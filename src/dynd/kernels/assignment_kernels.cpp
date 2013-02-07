//
// Copyright (C) 2011-13, DyND Developers
// BSD 2-Clause License, see LICENSE.txt
//

#include <dynd/dtype.hpp>
#include <dynd/kernels/assignment_kernels.hpp>
#include "single_assigner_builtin.hpp"

using namespace std;
using namespace dynd;

namespace {
    template<class T>
    struct aligned_fixed_size_copy_assign_type {
        static void single(char *dst, const char *src,
                        hierarchical_kernel_common_base *DYND_UNUSED(extra))
        {
            *(T *)dst = *(T *)src;
        }
    };

    template<int N>
    struct aligned_fixed_size_copy_assign;
    template<>
    struct aligned_fixed_size_copy_assign<1> {
        static void single(char *dst, const char *src,
                            hierarchical_kernel_common_base *DYND_UNUSED(extra))
        {
            *dst = *src;
        }
    };
    template<>
    struct aligned_fixed_size_copy_assign<2> : public aligned_fixed_size_copy_assign_type<int16_t> {};
    template<>
    struct aligned_fixed_size_copy_assign<4> : public aligned_fixed_size_copy_assign_type<int32_t> {};
    template<>
    struct aligned_fixed_size_copy_assign<8> : public aligned_fixed_size_copy_assign_type<int64_t> {};

    template<int N>
    struct unaligned_fixed_size_copy_assign {
        static void single(char *dst, const char *src,
                        hierarchical_kernel_common_base *DYND_UNUSED(extra))
        {
            memcpy(dst, src, N);
        }
    };
}
struct unaligned_copy_single_kernel_extra {
    hierarchical_kernel_common_base base;
    size_t data_size;
};
static void unaligned_copy_single(char *dst, const char *src, hierarchical_kernel_common_base *extra)
{
    size_t data_size = reinterpret_cast<unaligned_copy_single_kernel_extra *>(extra)->data_size;
    memcpy(dst, src, data_size);
}

size_t dynd::make_assignment_kernel(
                    hierarchical_kernel<unary_single_operation_t> *out,
                    size_t offset_out,
                    const dtype& dst_dt, const char *dst_metadata,
                    const dtype& src_dt, const char *src_metadata,
                    assign_error_mode errmode,
                    const dynd::eval::eval_context *ectx)
{
    if (errmode == assign_error_default && ectx != NULL) {
        errmode = ectx->default_assign_error_mode;
    }

    if (dst_dt.is_builtin()) {
        if (src_dt.is_builtin()) {
            // If the casting can be done losslessly, disable the error check to find faster code paths
            if (errmode != assign_error_none && is_lossless_assignment(dst_dt, src_dt)) {
                errmode = assign_error_none;
            }

            if (dst_dt.extended() == src_dt.extended()) {
                return make_pod_dtype_assignment_kernel(out, offset_out,
                                dst_dt.get_data_size(),dst_dt.get_alignment());
            } else {
                return make_builtin_dtype_assignment_function(out, offset_out,
                                dst_dt.get_type_id(), src_dt.get_type_id(),
                                errmode);
            }

        } else {
            return src_dt.extended()->make_assignment_kernel(out, offset_out,
                            dst_dt, dst_metadata,
                            src_dt, src_metadata,
                            errmode, ectx);
        }
    } else {
        return dst_dt.extended()->make_assignment_kernel(out, offset_out,
                        dst_dt, dst_metadata,
                        src_dt, src_metadata,
                        errmode, ectx);
    }
}

size_t dynd::make_pod_dtype_assignment_kernel(
                    hierarchical_kernel<unary_single_operation_t> *out,
                    size_t offset_out,
                    size_t data_size, size_t data_alignment)
{
    hierarchical_kernel_common_base *result = NULL;
    if (data_size == data_alignment) {
        // Aligned specialization tables
        // No need to reserve more space in the trivial cases, the space for a leaf is already there
        switch (data_size) {
            case 1:
                result = out->get_at<hierarchical_kernel_common_base>(offset_out);
                result->set_function<unary_single_operation_t>(&aligned_fixed_size_copy_assign<1>::single);
                return offset_out + sizeof(hierarchical_kernel_common_base);
            case 2:
                result = out->get_at<hierarchical_kernel_common_base>(offset_out);
                result->set_function<unary_single_operation_t>(&aligned_fixed_size_copy_assign<2>::single);
                return offset_out + sizeof(hierarchical_kernel_common_base);
            case 4:
                result = out->get_at<hierarchical_kernel_common_base>(offset_out);
                result->set_function<unary_single_operation_t>(&aligned_fixed_size_copy_assign<4>::single);
                return offset_out + sizeof(hierarchical_kernel_common_base);
            case 8:
                result = out->get_at<hierarchical_kernel_common_base>(offset_out);
                result->set_function<unary_single_operation_t>(&aligned_fixed_size_copy_assign<8>::single);
                return offset_out + sizeof(hierarchical_kernel_common_base);
            default:
                // Subtract the base amount to avoid over-reserving memory in this leaf case
                out->ensure_capacity(offset_out + sizeof(unaligned_copy_single_kernel_extra) -
                                sizeof(hierarchical_kernel_common_base));
                result = out->get_at<hierarchical_kernel_common_base>(offset_out);
                result->set_function<unary_single_operation_t>(&unaligned_copy_single);
                reinterpret_cast<unaligned_copy_single_kernel_extra *>(result)->data_size = data_size;
                return offset_out + sizeof(unaligned_copy_single_kernel_extra);
        }
    } else {
        // Unaligned specialization tables
        switch (data_size) {
            case 2:
                result = out->get_at<hierarchical_kernel_common_base>(offset_out);
                result->set_function<unary_single_operation_t>(unaligned_fixed_size_copy_assign<2>::single);
                return offset_out + sizeof(hierarchical_kernel_common_base);
            case 4:
                result = out->get_at<hierarchical_kernel_common_base>(offset_out);
                result->set_function<unary_single_operation_t>(unaligned_fixed_size_copy_assign<4>::single);
                return offset_out + sizeof(hierarchical_kernel_common_base);
            case 8:
                result = out->get_at<hierarchical_kernel_common_base>(offset_out);
                result->set_function<unary_single_operation_t>(unaligned_fixed_size_copy_assign<8>::single);
                return offset_out + sizeof(hierarchical_kernel_common_base);
            default:
                // Subtract the base amount to avoid over-reserving memory in this leaf case
                out->ensure_capacity(offset_out + sizeof(unaligned_copy_single_kernel_extra) -
                                sizeof(hierarchical_kernel_common_base));
                result = out->get_at<hierarchical_kernel_common_base>(offset_out);
                result->set_function<unary_single_operation_t>(&unaligned_copy_single);
                reinterpret_cast<unaligned_copy_single_kernel_extra *>(result)->data_size = data_size;
                return offset_out + sizeof(unaligned_copy_single_kernel_extra);
        }
    }
}

static unary_single_operation_t assign_table_single_kernel[builtin_type_id_count-2][builtin_type_id_count-2][4] =
{
#define SINGLE_OPERATION_PAIR_LEVEL(dst_type, src_type, errmode) \
            (unary_single_operation_t)&single_assigner_builtin<dst_type, src_type, errmode>::assign
        
#define ERROR_MODE_LEVEL(dst_type, src_type) { \
        SINGLE_OPERATION_PAIR_LEVEL(dst_type, src_type, assign_error_none), \
        SINGLE_OPERATION_PAIR_LEVEL(dst_type, src_type, assign_error_overflow), \
        SINGLE_OPERATION_PAIR_LEVEL(dst_type, src_type, assign_error_fractional), \
        SINGLE_OPERATION_PAIR_LEVEL(dst_type, src_type, assign_error_inexact) \
    }

#define SRC_TYPE_LEVEL(dst_type) { \
        ERROR_MODE_LEVEL(dst_type, dynd_bool), \
        ERROR_MODE_LEVEL(dst_type, int8_t), \
        ERROR_MODE_LEVEL(dst_type, int16_t), \
        ERROR_MODE_LEVEL(dst_type, int32_t), \
        ERROR_MODE_LEVEL(dst_type, int64_t), \
        ERROR_MODE_LEVEL(dst_type, uint8_t), \
        ERROR_MODE_LEVEL(dst_type, uint16_t), \
        ERROR_MODE_LEVEL(dst_type, uint32_t), \
        ERROR_MODE_LEVEL(dst_type, uint64_t), \
        ERROR_MODE_LEVEL(dst_type, float), \
        ERROR_MODE_LEVEL(dst_type, double), \
        ERROR_MODE_LEVEL(dst_type, complex<float>), \
        ERROR_MODE_LEVEL(dst_type, complex<double>) \
    }

    SRC_TYPE_LEVEL(dynd_bool),
    SRC_TYPE_LEVEL(int8_t),
    SRC_TYPE_LEVEL(int16_t),
    SRC_TYPE_LEVEL(int32_t),
    SRC_TYPE_LEVEL(int64_t),
    SRC_TYPE_LEVEL(uint8_t),
    SRC_TYPE_LEVEL(uint16_t),
    SRC_TYPE_LEVEL(uint32_t),
    SRC_TYPE_LEVEL(uint64_t),
    SRC_TYPE_LEVEL(float),
    SRC_TYPE_LEVEL(double),
    SRC_TYPE_LEVEL(complex<float>),
    SRC_TYPE_LEVEL(complex<double>)
#undef SRC_TYPE_LEVEL
#undef ERROR_MODE_LEVEL
#undef SINGLE_OPERATION_PAIR_LEVEL
};

size_t dynd::make_builtin_dtype_assignment_function(
                hierarchical_kernel<unary_single_operation_t> *out,
                size_t offset_out,
                type_id_t dst_type_id, type_id_t src_type_id,
                assign_error_mode errmode)
{
    // Do a table lookup for the built-in range of dtypes
    if (dst_type_id >= bool_type_id && dst_type_id <= complex_float64_type_id &&
                    src_type_id >= bool_type_id && src_type_id <= complex_float64_type_id &&
                    errmode != assign_error_default) {
        // No need to reserve more space, the space for a leaf is already there
        hierarchical_kernel_common_base *result = out->get_at<hierarchical_kernel_common_base>(offset_out);
        result->set_function<unary_single_operation_t>(assign_table_single_kernel[dst_type_id-bool_type_id][src_type_id-bool_type_id][errmode]);
        return offset_out + sizeof(hierarchical_kernel_common_base);
    } else {
        stringstream ss;
        ss << "Cannot assign from " << dtype(src_type_id) << " to " << dtype(dst_type_id);
        throw runtime_error(ss.str());
    }
}

void dynd::strided_assign_kernel_extra::single(char *dst, const char *src,
                    hierarchical_kernel_common_base *extra)
{
    extra_type *e = reinterpret_cast<extra_type *>(extra);
    hierarchical_kernel_common_base *echild = &(e + 1)->base;
    unary_single_operation_t opchild = echild->get_function<unary_single_operation_t>();
    intptr_t size = e->size, dst_stride = e->dst_stride, src_stride = e->src_stride;
    for (intptr_t i = 0; i < size; ++i, dst += dst_stride, src += src_stride) {
        opchild(dst, src, echild);
    }
}
void dynd::strided_assign_kernel_extra::destruct(
                hierarchical_kernel_common_base *extra)
{
    extra_type *e = reinterpret_cast<extra_type *>(extra);
    hierarchical_kernel_common_base *echild = &(e + 1)->base;
    if (echild->destructor) {
        echild->destructor(echild);
    }
}
