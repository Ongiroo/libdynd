//
// Copyright (C) 2011-13 Mark Wiebe, DyND Developers
// BSD 2-Clause License, see LICENSE.txt
//

#include <dynd/memblock/array_memory_block.hpp>
#include <dynd/array.hpp>
#include <dynd/shape_tools.hpp>

using namespace std;
using namespace dynd;

namespace dynd { namespace detail {

void free_array_memory_block(memory_block_data *memblock)
{
    array_preamble *preamble = reinterpret_cast<array_preamble *>(memblock);
    char *metadata = reinterpret_cast<char *>(preamble + 1);

    // Call the data destructor if necessary (i.e. the ndobject owns
    // the data memory, and the dtype has a data destructor)
    if (preamble->m_data_reference == NULL &&
                    !preamble->is_builtin_type() &&
                    (preamble->m_type->get_flags()&type_flag_destructor) != 0) {
        preamble->m_type->data_destruct(metadata, preamble->m_data_pointer);
    }

    // Free the references contained in the metadata
    if (!preamble->is_builtin_type()) {
        preamble->m_type->metadata_destruct(metadata);
        base_type_decref(preamble->m_type);
    }

    // Free the reference to the ndobject data
    if (preamble->m_data_reference != NULL) {
        memory_block_decref(preamble->m_data_reference);
    }

    // Finally free the memory block itself
    free(reinterpret_cast<void *>(memblock));
}

}} // namespace dynd::detail

memory_block_ptr dynd::make_array_memory_block(size_t metadata_size)
{
    char *result = (char *)malloc(sizeof(memory_block_data) + sizeof(array_preamble) + metadata_size);
    if (result == 0) {
        throw bad_alloc();
    }
    // Zero out all the metadata to start
    memset(result + sizeof(memory_block_data), 0, sizeof(array_preamble) + metadata_size);
    return memory_block_ptr(new (result) memory_block_data(1, array_memory_block_type), false);
}

memory_block_ptr dynd::make_array_memory_block(size_t metadata_size, size_t extra_size,
                    size_t extra_alignment, char **out_extra_ptr)
{
    size_t extra_offset = inc_to_alignment(sizeof(memory_block_data) + sizeof(array_preamble) + metadata_size,
                                        extra_alignment);
    char *result = (char *)malloc(extra_offset + extra_size);
    if (result == 0) {
        throw bad_alloc();
    }
    // Zero out all the metadata to start
    memset(result + sizeof(memory_block_data), 0, sizeof(array_preamble) + metadata_size);
    // Return a pointer to the extra allocated memory
    *out_extra_ptr = result + extra_offset;
    return memory_block_ptr(new (result) memory_block_data(1, array_memory_block_type), false);
}

memory_block_ptr dynd::make_array_memory_block(const ndt::type& dt, size_t ndim, const intptr_t *shape)
{
    size_t metadata_size, data_size;

    // Make sure that there are not too many 
    if (ndim > dt.get_undim()) {
        stringstream ss;
        ss << "Shape provided, ";
        print_shape(ss, ndim, shape);
        ss << ", has too many dimensions for type " << dt;
        throw runtime_error(ss.str());
    }

    if (dt.is_builtin()) {
        metadata_size = 0;
        data_size = dt.get_data_size();
    } else {
        metadata_size = dt.extended()->get_metadata_size();
        data_size = dt.extended()->get_default_data_size(ndim, shape);
    }

    char *data_ptr = NULL;
    memory_block_ptr result = make_array_memory_block(metadata_size, data_size, dt.get_data_alignment(), &data_ptr);

    if (dt.get_flags()&type_flag_zeroinit) {
        memset(data_ptr, 0, data_size);
    }

    array_preamble *preamble = reinterpret_cast<array_preamble *>(result.get());
    if (dt.is_builtin()) {
        preamble->m_type = reinterpret_cast<base_type *>(dt.get_type_id());
    } else {
        preamble->m_type = ndt::type(dt).release();
        preamble->m_type->metadata_default_construct(reinterpret_cast<char *>(preamble + 1), ndim, shape);
    }
    preamble->m_data_pointer = data_ptr;
    preamble->m_data_reference = NULL;
    preamble->m_flags = nd::read_access_flag|nd::write_access_flag;
    return result;
}

memory_block_ptr dynd::shallow_copy_array_memory_block(const memory_block_ptr& ndo)
{
    // Allocate the new memory block.
    const array_preamble *preamble = reinterpret_cast<const array_preamble *>(ndo.get());
    size_t metadata_size = 0;
    if (!preamble->is_builtin_type()) {
        metadata_size = preamble->m_type->get_metadata_size();
    }
    memory_block_ptr result = make_array_memory_block(metadata_size);
    array_preamble *result_preamble = reinterpret_cast<array_preamble *>(result.get());

    // Clone the data pointer
    result_preamble->m_data_pointer = preamble->m_data_pointer;
    result_preamble->m_data_reference = preamble->m_data_reference;
    if (result_preamble->m_data_reference == NULL) {
        result_preamble->m_data_reference = ndo.get();
    }
    memory_block_incref(result_preamble->m_data_reference);

    // Copy the flags
    result_preamble->m_flags = preamble->m_flags;

    // Clone the dtype
    result_preamble->m_type = preamble->m_type;
    if (!preamble->is_builtin_type()) {
        base_type_incref(preamble->m_type);
        preamble->m_type->metadata_copy_construct(reinterpret_cast<char *>(result.get()) + sizeof(array_preamble),
                        reinterpret_cast<const char *>(ndo.get()) + sizeof(array_preamble), ndo.get());
    }

    return result;
}

void dynd::array_memory_block_debug_print(const memory_block_data *memblock, std::ostream& o, const std::string& indent)
{
    const array_preamble *preamble = reinterpret_cast<const array_preamble *>(memblock);
    if (preamble->m_type != NULL) {
        ndt::type dt = preamble->is_builtin_type() ? ndt::type(preamble->get_type_id())
                        : ndt::type(preamble->m_type, true);
        o << indent << " dtype: " << dt << "\n";
    } else {
        o << indent << " uninitialized ndobject\n";
    }
}