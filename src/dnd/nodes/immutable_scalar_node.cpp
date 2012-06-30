//
// Copyright (C) 2011-2012, Dynamic NDArray Developers
// BSD 2-Clause License, see LICENSE.txt
//

#include <dnd/nodes/immutable_scalar_node.hpp>
#include <dnd/dtypes/conversion_dtype.hpp>

using namespace std;
using namespace dnd;

dnd::immutable_scalar_node::immutable_scalar_node(const dtype& dt, const char* data)
    : m_dtype(dt)
{
    if (dt.element_size() <= (intptr_t)sizeof(m_storage)) {
        m_data = reinterpret_cast<char *>(m_storage);
    } else {
        m_data = new char[dt.element_size()];
    }

    memcpy(m_data, data, dt.element_size());
}

dnd::immutable_scalar_node::~immutable_scalar_node()
{
    if (m_data != reinterpret_cast<const char *>(m_storage)) {
        delete[] m_data;
    }
}

ndarray_node_ptr dnd::immutable_scalar_node::as_dtype(const dtype& dt,
                    dnd::assign_error_mode errmode, bool allow_in_place)
{
    if (allow_in_place) {
        m_dtype = make_conversion_dtype(dt, m_dtype, errmode);
        return as_ndarray_node_ptr();
    } else {
        return make_immutable_scalar_node(
                        make_conversion_dtype(dt, m_dtype, errmode),
                        reinterpret_cast<const char *>(&m_data[0]));
    }
}

ndarray_node_ptr dnd::immutable_scalar_node::apply_linear_index(
                int DND_UNUSED(ndim), const bool *DND_UNUSED(remove_axis),
                const intptr_t *DND_UNUSED(start_index), const intptr_t *DND_UNUSED(index_strides),
                const intptr_t *DND_UNUSED(shape),
                bool DND_UNUSED(allow_in_place))
{
    return as_ndarray_node_ptr();
}

void dnd::immutable_scalar_node::debug_dump_extra(std::ostream& o, const std::string& indent) const
{
    o << indent << " data: ";
    hexadecimal_print(o, reinterpret_cast<const char *>(&m_data[0]), m_dtype.element_size());
    o << "\n";
}

ndarray_node_ptr dnd::make_immutable_scalar_node(const dtype& dt, const char* data)
{
    if (dt.is_object_type()) {
        throw runtime_error("immutable_scalar_node doesn't support object dtypes yet");
    }

    // Allocate the memory_block
    char *result = reinterpret_cast<char *>(malloc(sizeof(memory_block_data) + sizeof(immutable_scalar_node)));
    if (result == NULL) {
        throw bad_alloc();
    }
    // Placement new
    new (result + sizeof(memory_block_data))
            immutable_scalar_node(dt, data);
    return ndarray_node_ptr(new (result) memory_block_data(1, ndarray_node_memory_block_type), false);
}