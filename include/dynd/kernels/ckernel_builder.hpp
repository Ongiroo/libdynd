//
// Copyright (C) 2011-13 Mark Wiebe, DyND Developers
// BSD 2-Clause License, see LICENSE.txt
//

#ifndef _DYND__CKERNEL_BUILDER_HPP_
#define _DYND__CKERNEL_BUILDER_HPP_

#include <dynd/config.hpp>
#include <dynd/kernels/ckernel_prefix.hpp>
#include <dynd/kernels/ckernel_instance.hpp>

namespace dynd {

/**
 * Function pointers + data for a hierarchical
 * kernel which operates on type/metadata in
 * some configuration. Individual kernel types
 * are handled by the classes assignment_ckernel_builder, etc.
 *
 * The data placed in the kernel's data must
 * be relocatable with a memcpy, it must not rely on its
 * own address.
 */
class ckernel_builder {
    // Pointer to the kernel function pointers + data
    intptr_t *m_data;
    intptr_t m_capacity;
    // When the amount of data is small, this static data is used,
    // otherwise dynamic memory is allocated when it gets too big
    intptr_t m_static_data[16];

    inline bool using_static_data() const {
        return m_data == &m_static_data[0];
    }

    inline void destroy() {
        if (m_data != NULL) {
            ckernel_prefix *data;
            data = reinterpret_cast<ckernel_prefix *>(m_data);
            // Destroy whatever was created
            if (data->destructor != NULL) {
                data->destructor(data);
            }
            if (!using_static_data()) {
                // Free the memory
                free(data);
            }
        }
    }
protected:
public:
    ckernel_builder() {
        m_data = &m_static_data[0];
        m_capacity = sizeof(m_static_data);
        memset(m_static_data, 0, sizeof(m_static_data));
    }

    ~ckernel_builder() {
        destroy();
    }

    void reset() {
        destroy();
        m_data = &m_static_data[0];
        m_capacity = sizeof(m_static_data);
        memset(m_static_data, 0, sizeof(m_static_data));
    }

    /**
     * This function ensures that the kernel's data
     * is at least the required number of bytes. It
     * should only be called during the construction phase
     * of the kernel.
     *
     * NOTE: This function ensures that there is room for
     *       another base at the end, so if you are sure
     *       that you're a leaf kernel, use ensure_capacity_leaf
     *       instead.
     */
    inline void ensure_capacity(intptr_t requested_capacity) {
        ensure_capacity_leaf(requested_capacity +
                        sizeof(ckernel_prefix));
    }

    /**
     * This function ensures that the kernel's data
     * is at least the required number of bytes. It
     * should only be called during the construction phase
     * of the kernel when constructing a leaf kernel.
     */
    void ensure_capacity_leaf(intptr_t requested_capacity);

    /**
     * For use during construction, get's the kernel component
     * at the requested offset.
     */
    template<class T>
    T *get_at(size_t offset) {
        return reinterpret_cast<T *>(
                        reinterpret_cast<char *>(m_data) + offset);
    }

    ckernel_prefix *get() const {
        return reinterpret_cast<ckernel_prefix *>(m_data);
    }

    /**
     * Moves the kernel data held by this ckernel builder
     * into the provided ckernel_instance struct. Ownership
     * is transferred to 'cki'.
     *
     * Because the kernel size is not tracked by the ckernel_builder
     * object, but rather produced by the factory functions, it
     * is required as a parameter here.
     *
     * \param out  The ckernel_instance to populate.
     * \param kernel_size  The size, in bytes, of the ckernel_builder.
     */
    void move_into_cki(ckernel_instance *out, size_t kernel_size) {
        if (using_static_data()) {
            // Allocate some memory and move the kernel data into it
            out->kernel = reinterpret_cast<ckernel_prefix *>(malloc(kernel_size));
            if (out->kernel == NULL) {
                out->free_func = NULL;
                throw std::bad_alloc();
            }
            memcpy(out->kernel, m_data, kernel_size);
            memset(m_static_data, 0, sizeof(m_static_data));
        } else {
            // Use the existing kernel data memory
            out->kernel = reinterpret_cast<ckernel_prefix *>(m_data);
            // Switch this kernel back to an empty static data kernel
            m_data = &m_static_data[0];
            m_capacity = sizeof(m_static_data);
            memset(m_static_data, 0, sizeof(m_static_data));
        }
        out->kernel_size = kernel_size;
        out->free_func = free;
    }

    friend int ckernel_builder_ensure_capacity_leaf(void *ckb, intptr_t requested_capacity);
};

/**
 * C API function for constructing a ckernel_builder object
 * in place. The `ckb` pointer must point to memory which
 * has sizeof(ckernel_builder) bytes (== 18 * sizeof(void *)),
 * and is aligned appropriately (to sizeof(void *)).
 *
 * After a ckernel_builder instance is initialized this way,
 * all the other ckernel_builder functions can be used on it,
 * and when it is no longer needed, it must be destructed
 * by calling `ckernel_builder_destruct`.
 *
 * \param ckb  Pointer to the ckernel_builder instance. Must have
 *             size 18 * sizeof(void *), and alignment sizeof(void *).
 */
inline void ckernel_builder_construct(void *ckb)
{
    // Use the placement new operator to initialize in-place
    new (ckb) ckernel_builder();
}

/**
 * C API function for destroying a valid ckernel_builder object
 * in place. The `ckb` pointer must point to memory which
 * was previously initialized with `ckernel_buidler_construct`
 *
 * \param ckb  Pointer to the ckernel_builder instance.
 */
inline void ckernel_builder_destruct(void *ckb)
{
    // Call the destructor
    ckernel_builder *ckb_ptr = reinterpret_cast<ckernel_builder *>(ckb);
    ckb_ptr->~ckernel_builder();
}

/**
 * C API function for resetting a valid ckernel_builder object
 * to an uninitialized state.
 *
 * \param ckb  Pointer to the ckernel_builder instance.
 */
inline void ckernel_builder_reset(void *ckb)
{
    ckernel_builder *ckb_ptr = reinterpret_cast<ckernel_builder *>(ckb);
    ckb_ptr->reset();
}

/**
 * C API function for ensuring that the kernel's data
 * is at least the required number of bytes. It
 * should only be called during the construction phase
 * of the kernel when constructing a leaf kernel.
 *
 * If the created kernel has a child kernel, use
 * the function `ckernel_builder_ensure_capacity` instead.
 *
 * \param ckb  Pointer to the ckernel_builder instance.
 * \param requested_capacity  The number of bytes required by the ckernel.
 *
 * \returns  0 on success, -1 on memory allocation failure.
 */
inline int ckernel_builder_ensure_capacity_leaf(void *ckb, intptr_t requested_capacity)
{
    ckernel_builder *ckb_ptr = reinterpret_cast<ckernel_builder *>(ckb);
    if (ckb_ptr->m_capacity < requested_capacity) {
        // Grow by a factor of 1.5
        // https://github.com/facebook/folly/blob/master/folly/docs/FBVector.md
        intptr_t grown_capacity = ckb_ptr->m_capacity * 3 / 2;
        if (requested_capacity < grown_capacity) {
            requested_capacity = grown_capacity;
        }
        intptr_t *new_data;
        if (ckb_ptr->using_static_data()) {
            // If we were previously using the static data, do a malloc
            new_data = reinterpret_cast<intptr_t *>(malloc(requested_capacity));
            // If the allocation succeeded, copy the old data as the realloc would
            if (new_data != NULL) {
                memcpy(new_data, ckb_ptr->m_data, ckb_ptr->m_capacity);
            }
        } else {
            // Otherwise do a realloc
            new_data = reinterpret_cast<intptr_t *>(realloc(
                            ckb_ptr->m_data, requested_capacity));
        }
        if (new_data == NULL) {
            ckb_ptr->destroy();
            ckb_ptr->m_data = NULL;
            return -1;
        }
        // Zero out the newly allocated capacity
        memset(reinterpret_cast<char *>(new_data) + ckb_ptr->m_capacity,
                        0, requested_capacity - ckb_ptr->m_capacity);
        ckb_ptr->m_data = new_data;
        ckb_ptr->m_capacity = requested_capacity;
    }
    return 0;
}

/**
 * C API function for ensuring that the kernel's data
 * is at least the required number of bytes. It
 * should only be called during the construction phase
 * of the kernel when constructing a leaf kernel.
 *
 * This function allocates the requested capacity, plus enough
 * space for an empty child kernel to ensure safe destruction
 * during error handling. If a leaf kernel is being constructed,
 * use `ckernel_builder_ensure_capacity_leaf` instead.
 *
 * \param ckb  Pointer to the ckernel_builder instance.
 * \param requested_capacity  The number of bytes required by the ckernel.
 *
 * \returns  0 on success, -1 on memory allocation failure.
 */
inline int ckernel_builder_ensure_capacity(void *ckb, intptr_t requested_capacity)
{
    return ckernel_builder_ensure_capacity_leaf(ckb,
                    requested_capacity + sizeof(ckernel_prefix));
}

inline void ckernel_builder::ensure_capacity_leaf(intptr_t requested_capacity)
{
    if (ckernel_builder_ensure_capacity_leaf(this, requested_capacity) < 0) {
        throw std::bad_alloc();
    }
}

} // namespace dynd


#endif // _DYND__CKERNEL_BUILDER_HPP_
