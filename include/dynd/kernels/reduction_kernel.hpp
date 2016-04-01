//
// Copyright (C) 2011-16 DyND Developers
// BSD 2-Clause License, see LICENSE.txt
//

#pragma once

#include <dynd/callable.hpp>
#include <dynd/kernels/base_kernel.hpp>
#include <dynd/assignment.hpp>
#include <dynd/functional.hpp>
#include <dynd/kernels/constant_kernel.hpp>
#include <dynd/kernels/reduction_kernel_prefix.hpp>

namespace dynd {
namespace nd {
  namespace functional {

    struct reduction_data_type {
      array identity;
      std::intptr_t ndim; // total number of dimensions being processed
      std::intptr_t naxis;
      const int32 *axes;
      bool keepdims;
      std::intptr_t stored_ndim; // original number of dimensions

      std::intptr_t init_offset;
      char *child_data;

      reduction_data_type() : ndim(0), naxis(0), axes(NULL), keepdims(false), stored_ndim(0) {}

      bool is_broadcast() const { return axes != NULL && (naxis == 0 || stored_ndim - axes[0] != ndim); }

      bool is_inner() const { return ndim == 1; }
    };

    template <typename SelfType>
    struct base_reduction_kernel : reduction_kernel_prefix {
      typedef reduction_data_type data_type;

      /**
       * Returns the child kernel immediately following this one.
       */
      kernel_prefix *get_child() { return kernel_prefix::get_child(sizeof(SelfType)); }

      kernel_prefix *get_child(intptr_t offset)
      {
        return kernel_prefix::get_child(kernel_builder::aligned_size(offset));
      }

      reduction_kernel_prefix *get_reduction_child()
      {
        return reinterpret_cast<reduction_kernel_prefix *>(this->get_child());
      }

      void call(array *dst, const array *src)
      {
        char *src_data[1];
        for (size_t i = 0; i < 1; ++i) {
          src_data[i] = const_cast<char *>(src[i].cdata());
        }
        reinterpret_cast<SelfType *>(this)->single_first(const_cast<char *>(dst->cdata()), src_data);
      }

      static void call_wrapper(kernel_prefix *self, array *dst, array *src)
      {
        reinterpret_cast<SelfType *>(self)->call(dst, src);
      }

      template <typename... ArgTypes>
      static void init(SelfType *self, kernel_request_t kernreq, ArgTypes &&... args)
      {
        new (self) SelfType(std::forward<ArgTypes>(args)...);

        self->destructor = SelfType::destruct;
        // Get the function pointer for the first_call
        switch (kernreq) {
        case kernel_request_call:
          self->set_first_call_function(SelfType::call_wrapper);
          break;
        case kernel_request_single:
          self->set_first_call_function(SelfType::single_first_wrapper);
          break;
        case kernel_request_strided:
          self->set_first_call_function(SelfType::strided_first_wrapper);
          break;
        default:
          std::stringstream ss;
          ss << "make_lifted_reduction_ckernel: unrecognized request " << (int)kernreq;
          throw std::runtime_error(ss.str());
        }
        // The function pointer for followup accumulation calls
        self->set_followup_call_function(SelfType::strided_followup_wrapper);
      }

      static void destruct(kernel_prefix *self) { reinterpret_cast<SelfType *>(self)->~SelfType(); }

      constexpr size_t size() const { return sizeof(SelfType); }

      static void single_first_wrapper(kernel_prefix *self, char *dst, char *const *src)
      {
        return reinterpret_cast<SelfType *>(self)->single_first(dst, src);
      }

      static void strided_first_wrapper(kernel_prefix *self, char *dst, intptr_t dst_stride, char *const *src,
                                        const intptr_t *src_stride, size_t count)

      {
        return reinterpret_cast<SelfType *>(self)->strided_first(dst, dst_stride, src, src_stride, count);
      }

      static void strided_followup_wrapper(kernel_prefix *self, char *dst, intptr_t dst_stride, char *const *src,
                                           const intptr_t *src_stride, size_t count)

      {
        return reinterpret_cast<SelfType *>(self)->strided_followup(dst, dst_stride, src, src_stride, count);
      }
    };

    /**
     * STRIDED INITIAL REDUCTION DIMENSION
     * This ckernel handles one dimension of the reduction processing,
     * where:
     *  - It's a reduction dimension, so dst_stride is zero.
     *  - It's an initial dimension, there are additional dimensions
     *    being processed by its child kernels.
     *  - The source data is strided.
     *
     * Requirements:
     *  - The child first_call function must be *single*.
     *  - The child followup_call function must be *strided*.
     *
     */
    template <type_id_t Src0TypeID, bool Broadcast, bool Inner>
    struct reduction_kernel;

    template <>
    struct reduction_kernel<fixed_dim_id, false, false>
        : base_reduction_kernel<reduction_kernel<fixed_dim_id, false, false>> {
      std::intptr_t src0_element_size;
      std::intptr_t src0_element_stride;

      reduction_kernel(std::intptr_t src0_element_size, std::intptr_t src_stride)
          : src0_element_size(src0_element_size), src0_element_stride(src_stride)
      {
      }

      ~reduction_kernel() { get_child()->destroy(); }

      void single_first(char *dst, char *const *src)
      {
        reduction_kernel_prefix *child = get_reduction_child();
        // The first call at the "dst" address
        child->single_first(dst, src);
        if (src0_element_size > 1) {
          // All the followup calls at the "dst" address
          char *src_second = src[0] + src0_element_stride;
          child->strided_followup(dst, 0, &src_second, &src0_element_stride, src0_element_size - 1);
        }
      }

      void strided_first(char *dst, intptr_t dst_stride, char *const *src, const intptr_t *src_stride, size_t count)
      {
        reduction_kernel_prefix *child = get_reduction_child();

        char *src0 = src[0];
        intptr_t src0_stride = src_stride[0];
        if (dst_stride == 0) {
          // With a zero stride, we have one "first", followed by many
          // "followup" calls
          child->single_first(dst, &src0);
          if (src0_element_size > 1) {
            char *inner_src_second = src0 + src0_element_stride;
            child->strided_followup(dst, 0, &inner_src_second, &src0_element_stride, src0_element_size - 1);
          }
          src0 += src0_stride;
          for (std::size_t i = 1; i != count; ++i) {
            child->strided_followup(dst, 0, &src0, &src0_element_stride, src0_element_size);
            src0 += src0_stride;
          }
        }
        else {
          // With a non-zero stride, each iteration of the outer loop is
          // "first"
          for (size_t i = 0; i != count; ++i) {
            child->single_first(dst, &src0);
            if (src0_element_size > 1) {
              char *inner_src_second = src0 + src0_element_stride;
              child->strided_followup(dst, 0, &inner_src_second, &src0_element_stride, src0_element_size - 1);
            }
            dst += dst_stride;
            src0 += src0_stride;
          }
        }
      }

      void strided_followup(char *dst, intptr_t dst_stride, char *const *src, const intptr_t *src_stride, size_t count)
      {
        reduction_kernel_prefix *child = get_reduction_child();

        char *src0 = src[0];
        intptr_t src0_stride = src_stride[0];
        for (size_t i = 0; i != count; ++i) {
          child->strided_followup(dst, 0, &src0, &src0_element_stride, src0_element_size);
          dst += dst_stride;
          src0 += src0_stride;
        }
      }

      /**
       * Adds a ckernel layer for processing one dimension of the reduction.
       * This is for a strided dimension which is being reduced, and is not
       * the final dimension before the accumulation operation.
       */
      static void instantiate(callable &self, callable &DYND_UNUSED(child), char *data, kernel_builder *ckb,
                              const ndt::type &dst_tp, const char *dst_arrmeta, intptr_t nsrc, const ndt::type *src_tp,
                              const char *const *src_arrmeta, kernel_request_t kernreq, intptr_t nkwd,
                              const array *kwds, const std::map<std::string, ndt::type> &tp_vars)
      {
        const ndt::type &src0_element_tp = src_tp[0].extended<ndt::fixed_dim_type>()->get_element_type();
        const char *src0_element_arrmeta = src_arrmeta[0] + sizeof(size_stride_t);

        intptr_t src_size = src_tp[0].extended<ndt::fixed_dim_type>()->get_fixed_dim_size();
        intptr_t src_stride = src_tp[0].extended<ndt::fixed_dim_type>()->get_fixed_stride(src_arrmeta[0]);

        ckb->emplace_back<reduction_kernel>(kernreq, src_size, src_stride);

        --reinterpret_cast<data_type *>(data)->ndim;
        --reinterpret_cast<data_type *>(data)->naxis;
        if (reinterpret_cast<data_type *>(data)->axes != NULL) {
          ++reinterpret_cast<data_type *>(data)->axes;
        }

        if (reinterpret_cast<data_type *>(data)->keepdims) {
          const ndt::type &dst_element_tp = dst_tp.extended<ndt::fixed_dim_type>()->get_element_type();
          const char *dst_element_arrmeta = dst_arrmeta + sizeof(size_stride_t);

          return self->instantiate(nullptr, data, ckb, dst_element_tp, dst_element_arrmeta, nsrc, &src0_element_tp,
                                   &src0_element_arrmeta, kernel_request_single, nkwd, kwds, tp_vars);
        }

        return self->instantiate(nullptr, data, ckb, dst_tp, dst_arrmeta, nsrc, &src0_element_tp, &src0_element_arrmeta,
                                 kernel_request_single, nkwd, kwds, tp_vars);
      }
    };

    /**
     * STRIDED INNER REDUCTION DIMENSION
     * This ckernel handles one dimension of the reduction processing,
     * where:
     *  - It's a reduction dimension, so dst_stride is zero.
     *  - It's an inner dimension, calling the reduction kernel directly.
     *  - The source data is strided.
     *
     * Requirements:
     *  - The child destination initialization kernel must be *single*.
     *  - The child reduction kernel must be *strided*.
     *
     */
    template <>
    struct reduction_kernel<fixed_dim_id, false, true>
        : base_reduction_kernel<reduction_kernel<fixed_dim_id, false, true>> {
      // The code assumes that size >= 1
      intptr_t size_first;
      intptr_t src_stride_first;
      intptr_t _size;
      intptr_t src_stride;
      size_t init_offset;

      ~reduction_kernel()
      {
        get_child()->destroy();
        get_child(init_offset)->destroy();
      }

      void single_first(char *dst, char *const *src)
      {
        char *src0 = src[0];

        // Initialize the dst values
        get_child(init_offset)->single(dst, src);
        src0 += src_stride_first;

        // Do the reduction
        get_child()->strided(dst, 0, &src0, &src_stride, size_first);
      }

      void strided_first(char *dst, intptr_t dst_stride, char *const *src, const intptr_t *src_stride, size_t count)
      {
        kernel_prefix *init_child = get_child(init_offset);
        kernel_prefix *reduction_child = get_child();

        char *src0 = src[0];
        if (dst_stride == 0) {
          // With a zero stride, we initialize "dst" once, then do many
          // accumulations
          init_child->single(dst, &src0);
          src0 += src_stride_first;

          reduction_child->strided(dst, 0, &src0, &this->src_stride, size_first);

          for (std::size_t i = 1; i != count; ++i) {
            reduction_child->strided(dst, 0, &src0, &this->src_stride, size_first);
            dst += dst_stride;
            src0 += src_stride[0];
          }
        }
        else {
          // With a non-zero stride, each iteration of the outer loop has to
          // initialize then reduce
          for (size_t i = 0; i != count; ++i) {
            init_child->single(dst, &src0);

            char *inner_child_src = src0 + src_stride_first;
            reduction_child->strided(dst, 0, &inner_child_src, &this->src_stride, size_first);
            dst += dst_stride;
            src0 += src_stride[0];
          }
        }
      }

      void strided_followup(char *dst, intptr_t dst_stride, char *const *src, const intptr_t *src_stride, size_t count)
      {
        kernel_prefix *reduce_child = get_child();

        // No initialization, all reduction
        char *src0 = src[0];
        for (size_t i = 0; i != count; ++i) {
          reduce_child->strided(dst, 0, &src0, &this->src_stride, _size);
          dst += dst_stride;
          src0 += src_stride[0];
        }
      }

      /**
       * Adds a ckernel layer for processing one dimension of the reduction.
       * This is for a strided dimension which is being reduced, and is
       * the final dimension before the accumulation operation.
       */
      static void instantiate(callable &self, callable &DYND_UNUSED(child), char *data, kernel_builder *ckb,
                              const ndt::type &dst_tp, const char *dst_arrmeta, intptr_t nsrc, const ndt::type *src_tp,
                              const char *const *src_arrmeta, kernel_request_t kernreq, intptr_t nkwd,
                              const array *kwds, const std::map<std::string, ndt::type> &tp_vars)
      {
        const ndt::type &src0_element_tp = src_tp[0].extended<ndt::fixed_dim_type>()->get_element_type();
        const char *src0_element_arrmeta = src_arrmeta[0] + sizeof(size_stride_t);

        intptr_t src_size = src_tp[0].extended<ndt::fixed_dim_type>()->get_fixed_dim_size();
        intptr_t src_stride = src_tp[0].extended<ndt::fixed_dim_type>()->get_fixed_stride(src_arrmeta[0]);

        intptr_t root_ckb_offset = ckb->size();
        ckb->emplace_back<reduction_kernel>(kernreq);
        reduction_kernel *e = ckb->get_at<reduction_kernel>(root_ckb_offset);
        // The striding parameters
        e->src_stride = src_stride;
        e->_size = src_size;
        if (reinterpret_cast<data_type *>(data)->identity.is_null()) {
          e->size_first = e->_size - 1;
          e->src_stride_first = e->src_stride;
        }
        else {
          e->size_first = e->_size;
          e->src_stride_first = 0;
        }

        // Need to retrieve 'e' again because it may have moved

        --reinterpret_cast<data_type *>(data)->ndim;
        --reinterpret_cast<data_type *>(data)->naxis;
        if (reinterpret_cast<data_type *>(data)->axes != NULL) {
          ++reinterpret_cast<data_type *>(data)->axes;
        }

        if (reinterpret_cast<data_type *>(data)->keepdims) {
          const ndt::type &dst_element_tp = dst_tp.extended<ndt::fixed_dim_type>()->get_element_type();
          const char *dst_element_arrmeta = dst_arrmeta + sizeof(size_stride_t);

          self->instantiate(nullptr, data, ckb, dst_element_tp, dst_element_arrmeta, nsrc, &src0_element_tp,
                            &src0_element_arrmeta, kernel_request_single, nkwd, kwds, tp_vars);
        }
        else {
          self->instantiate(nullptr, data, ckb, dst_tp, dst_arrmeta, nsrc, &src0_element_tp, &src0_element_arrmeta,
                            kernel_request_single, nkwd, kwds, tp_vars);
        }

        e = reinterpret_cast<kernel_builder *>(ckb)->get_at<reduction_kernel>(root_ckb_offset);
        e->init_offset = reinterpret_cast<data_type *>(data)->init_offset - root_ckb_offset;

        delete reinterpret_cast<data_type *>(data);
      }
    };

    template <>
    struct reduction_kernel<var_dim_id, false, true>
        : base_reduction_kernel<reduction_kernel<var_dim_id, false, true>> {
      intptr_t src0_inner_stride;
      intptr_t src0_inner_stride_first;
      intptr_t init_offset;

      reduction_kernel(std::intptr_t src0_inner_stride, bool with_identity = false)
          : src0_inner_stride(src0_inner_stride)
      {
        if (with_identity) {
          src0_inner_stride_first = 0;
        }
        else {
          src0_inner_stride_first = src0_inner_stride;
        }
      }

      ~reduction_kernel()
      {
        get_child(init_offset)->destroy();
        get_child()->destroy();
      }

      void single_first(char *dst, char *const *src)
      {
        size_t inner_size = reinterpret_cast<ndt::var_dim_type::data_type *>(src[0])->size;
        if (src0_inner_stride_first != 0) {
          --inner_size;
        }

        char *src0_data = reinterpret_cast<ndt::var_dim_type::data_type *>(src[0])->begin;
        get_child(init_offset)->single(dst, &src0_data);
        src0_data += src0_inner_stride_first;

        get_child()->strided(dst, 0, &src0_data, &src0_inner_stride, inner_size);
      }

      void strided_first(char *dst, intptr_t dst_stride, char *const *src, const intptr_t *src_stride, size_t count)
      {
        kernel_prefix *init_child = get_child(init_offset);
        kernel_prefix *reduction_child = get_child();

        char *src0 = src[0];
        for (size_t i = 0; i != count; ++i) {
          char *src0_data = reinterpret_cast<ndt::var_dim_type::data_type *>(src0)->begin;
          init_child->single(dst, &src0_data);

          size_t inner_size = reinterpret_cast<ndt::var_dim_type::data_type *>(src0)->size;
          if (src0_inner_stride_first != 0) {
            --inner_size;
          }

          src0_data += src0_inner_stride_first;
          reduction_child->strided(dst, 0, &src0_data, &src0_inner_stride,
                                   reinterpret_cast<ndt::var_dim_type::data_type *>(src0)->size - 1);
          dst += dst_stride;
          src0 += src_stride[0];
        }
      }

      void strided_followup(char *dst, intptr_t dst_stride, char *const *src, const intptr_t *src_stride, size_t size)
      {
        kernel_prefix *child = get_child();

        char *src0 = src[0];
        for (size_t i = 0; i != size; ++i) {
          child->strided(dst, 0, &reinterpret_cast<ndt::var_dim_type::data_type *>(src0)->begin, &src0_inner_stride,
                         reinterpret_cast<ndt::var_dim_type::data_type *>(src0)->size);
          dst += dst_stride;
          src0 += src_stride[0];
        }
      }

      static void instantiate(callable &self, callable &DYND_UNUSED(child), char *data, kernel_builder *ckb,
                              const ndt::type &dst_tp, const char *dst_arrmeta, intptr_t nsrc, const ndt::type *src_tp,
                              const char *const *src_arrmeta, kernel_request_t kernreq, intptr_t nkwd,
                              const array *kwds, const std::map<std::string, ndt::type> &tp_vars)
      {
        const ndt::type &src0_element_tp = src_tp[0].extended<ndt::var_dim_type>()->get_element_type();
        const char *src0_element_arrmeta = src_arrmeta[0] + sizeof(ndt::var_dim_type::metadata_type);

        intptr_t root_ckb_offset = ckb->size();
        ckb->emplace_back<reduction_kernel>(
            kernreq, reinterpret_cast<const ndt::var_dim_type::metadata_type *>(src_arrmeta[0])->stride);

        --reinterpret_cast<data_type *>(data)->ndim;
        --reinterpret_cast<data_type *>(data)->naxis;

        self->instantiate(nullptr, data, ckb, dst_tp, dst_arrmeta, nsrc, &src0_element_tp, &src0_element_arrmeta,
                          kernel_request_single, nkwd, kwds, tp_vars);

        reduction_kernel *self_k = reinterpret_cast<kernel_builder *>(ckb)->get_at<reduction_kernel>(root_ckb_offset);
        self_k->init_offset = reinterpret_cast<data_type *>(data)->init_offset - root_ckb_offset;

        delete reinterpret_cast<data_type *>(data);
      }
    };

    /**
     * STRIDED INITIAL BROADCAST DIMENSION
     * This ckernel handles one dimension of the reduction processing,
     * where:
     *  - It's a broadcast dimension, so dst_stride is not zero.
     *  - It's an initial dimension, there are additional dimensions
     *    being processed after this one.
     *  - The source data is strided.
     *
     * Requirements:
     *  - The child first_call function must be *strided*.
     *  - The child followup_call function must be *strided*.
     *
     */
    template <>
    struct reduction_kernel<fixed_dim_id, true, false>
        : base_reduction_kernel<reduction_kernel<fixed_dim_id, true, false>> {
      intptr_t _size;
      intptr_t dst_stride, src_stride;

      reduction_kernel(std::intptr_t size, std::intptr_t dst_stride, std::intptr_t src_stride)
          : _size(size), dst_stride(dst_stride), src_stride(src_stride)
      {
      }

      ~reduction_kernel() { get_child()->destroy(); }

      void single_first(char *dst, char *const *src)
      {
        reduction_kernel_prefix *child = get_reduction_child();
        child->strided_first(dst, dst_stride, src, &src_stride, _size);
      }

      void strided_first(char *dst, intptr_t dst_stride, char *const *src, const intptr_t *src_stride, size_t count)
      {
        reduction_kernel_prefix *echild = reinterpret_cast<reduction_kernel_prefix *>(this->get_child());
        kernel_strided_t opchild_first_call = echild->get_first_call_function<kernel_strided_t>();
        kernel_strided_t opchild_followup_call = echild->get_followup_call_function();
        intptr_t inner_size = this->_size;
        intptr_t inner_dst_stride = this->dst_stride;
        intptr_t inner_src_stride = this->src_stride;
        char *src0 = src[0];
        intptr_t src0_stride = src_stride[0];
        if (dst_stride == 0) {
          // With a zero stride, we have one "first", followed by many
          // "followup"
          // calls
          opchild_first_call(echild, dst, inner_dst_stride, &src0, &inner_src_stride, inner_size);
          dst += dst_stride;
          src0 += src0_stride;
          for (intptr_t i = 1; i < (intptr_t)count; ++i) {
            opchild_followup_call(echild, dst, inner_dst_stride, &src0, &inner_src_stride, inner_size);
            dst += dst_stride;
            src0 += src0_stride;
          }
        }
        else {
          // With a non-zero stride, each iteration of the outer loop is
          // "first"
          for (size_t i = 0; i != count; ++i) {
            opchild_first_call(echild, dst, inner_dst_stride, &src0, &inner_src_stride, inner_size);
            dst += dst_stride;
            src0 += src0_stride;
          }
        }
      }

      void strided_followup(char *dst, intptr_t dst_stride, char *const *src, const intptr_t *src_stride, size_t count)
      {
        reduction_kernel_prefix *reduction_child = this->get_reduction_child();

        char *src0 = src[0];
        for (size_t i = 0; i != count; ++i) {
          reduction_child->strided_followup(dst, this->dst_stride, &src0, &this->src_stride, this->_size);
          dst += dst_stride;
          src0 += src_stride[0];
        }
      }

      /**
       * Adds a ckernel layer for processing one dimension of the reduction.
       * This is for a strided dimension which is being broadcast, and is not
       * the final dimension before the accumulation operation.
       */
      static void instantiate(callable &self, callable &DYND_UNUSED(child), char *data, kernel_builder *ckb,
                              const ndt::type &dst_tp, const char *dst_arrmeta, intptr_t nsrc, const ndt::type *src_tp,
                              const char *const *src_arrmeta, kernel_request_t kernreq, intptr_t nkwd,
                              const array *kwds, const std::map<std::string, ndt::type> &tp_vars)
      {
        intptr_t src_size = src_tp[0].extended<ndt::fixed_dim_type>()->get_fixed_dim_size();
        intptr_t src_stride = src_tp[0].extended<ndt::fixed_dim_type>()->get_fixed_stride(src_arrmeta[0]);

        intptr_t dst_stride = dst_tp.extended<ndt::fixed_dim_type>()->get_fixed_stride(dst_arrmeta);

        ckb->emplace_back<reduction_kernel>(kernreq, src_size, dst_stride, src_stride);

        --reinterpret_cast<data_type *>(data)->ndim;

        const ndt::type &src0_element_tp = src_tp[0].extended<ndt::fixed_dim_type>()->get_element_type();
        const char *src0_element_arrmeta = src_arrmeta[0] + sizeof(size_stride_t);
        const ndt::type &dst_element_tp = dst_tp.extended<ndt::fixed_dim_type>()->get_element_type();
        const char *dst_element_arrmeta = dst_arrmeta + sizeof(size_stride_t);

        return self->instantiate(nullptr, data, ckb, dst_element_tp, dst_element_arrmeta, nsrc, &src0_element_tp,
                                 &src0_element_arrmeta, kernel_request_strided, nkwd, kwds, tp_vars);
      }
    };

    /**
     * STRIDED INNER BROADCAST DIMENSION
     * This ckernel handles one dimension of the reduction processing,
     * where:
     *  - It's a broadcast dimension, so dst_stride is not zero.
     *  - It's an inner dimension, calling the reduction kernel directly.
     *  - The source data is strided.
     *
     * Requirements:
     *  - The child reduction kernel must be *strided*.
     *  - The child destination initialization kernel must be *strided*.
     *
     */
    template <>
    struct reduction_kernel<fixed_dim_id, true, true>
        : base_reduction_kernel<reduction_kernel<fixed_dim_id, true, true>> {
      // The code assumes that size >= 1
      intptr_t _size;
      intptr_t dst_stride, src_stride;
      size_t dst_init_kernel_offset;

      intptr_t size_first;
      intptr_t dst_stride_first;
      intptr_t src_stride_first;

      reduction_kernel(intptr_t dst_stride, intptr_t src_stride) : dst_stride(dst_stride), src_stride(src_stride) {}

      ~reduction_kernel()
      {
        // The reduction kernel
        get_child()->destroy();
        // The destination initialization kernel
        get_child(dst_init_kernel_offset)->destroy();
      }

      void single_first(char *dst, char *const *src)
      {
        // Initialize the dst values
        get_child(dst_init_kernel_offset)->strided(dst, dst_stride, src, &src_stride_first, _size);
        if (src_stride_first == 0) {
          // Then do the accumulation
          get_child()->strided(dst, dst_stride, src, &src_stride, _size);
        }
      }

      void strided_first(char *dst, intptr_t dst_stride, char *const *src, const intptr_t *src_stride, size_t count)
      {
        kernel_prefix *init_child = get_child(dst_init_kernel_offset);
        kernel_prefix *reduction_child = get_child();

        intptr_t inner_size = this->_size;
        intptr_t inner_dst_stride = this->dst_stride;
        intptr_t inner_src_stride = this->src_stride;
        char *src0 = src[0];
        intptr_t src0_stride = src_stride[0];
        if (dst_stride == 0) {
          // With a zero stride, we initialize "dst" once, then do many
          // accumulations
          init_child->strided(dst, inner_dst_stride, &src0, &this->src_stride_first, inner_size);
          dst += dst_stride_first;
          src0 += src_stride_first;
          for (size_t i = 1; i != count; ++i) {
            reduction_child->strided(dst, inner_dst_stride, &src0, &inner_src_stride, inner_size);
            src0 += src0_stride;
          }
        }
        else {
          // With a non-zero stride, every iteration is an initialization
          for (size_t i = 0; i != count; ++i) {
            init_child->strided(dst, inner_dst_stride, &src0, &src_stride_first, _size);
            if (src_stride_first == 0) {
              reduction_child->strided(dst, inner_dst_stride, &src0, &inner_src_stride, _size);
            }

            dst += dst_stride;
            src0 += src0_stride;
          }
        }
      }

      void strided_followup(char *dst, intptr_t dst_stride, char *const *src, const intptr_t *src_stride, size_t count)
      {
        kernel_prefix *echild_reduce = get_child();
        // No initialization, all reduction
        kernel_strided_t opchild_reduce = echild_reduce->get_function<kernel_strided_t>();
        intptr_t inner_size = this->_size;
        intptr_t inner_dst_stride = this->dst_stride;
        intptr_t inner_src_stride = this->src_stride;
        char *src0 = src[0];
        intptr_t src0_stride = src_stride[0];
        for (size_t i = 0; i != count; ++i) {
          opchild_reduce(echild_reduce, dst, inner_dst_stride, &src0, &inner_src_stride, inner_size);
          dst += dst_stride;
          src0 += src0_stride;
        }
      }

      /**
       * Adds a ckernel layer for processing one dimension of the reduction.
       * This is for a strided dimension which is being broadcast, and is
       * the final dimension before the accumulation operation.
       */
      static void instantiate(callable &self, callable &DYND_UNUSED(child), char *data, kernel_builder *ckb,
                              const ndt::type &dst_tp, const char *dst_arrmeta, intptr_t nsrc, const ndt::type *src_tp,
                              const char *const *src_arrmeta, kernel_request_t kernreq, intptr_t nkwd,
                              const array *kwds, const std::map<std::string, ndt::type> &tp_vars)
      {
        const ndt::type &src0_element_tp = src_tp[0].extended<ndt::base_dim_type>()->get_element_type();
        const char *src0_element_arrmeta = src_arrmeta[0] + sizeof(size_stride_t);

        intptr_t src_size = src_tp[0].extended<ndt::fixed_dim_type>()->get_fixed_dim_size();
        intptr_t src_stride = src_tp[0].extended<ndt::fixed_dim_type>()->get_fixed_stride(src_arrmeta[0]);
        intptr_t dst_stride = dst_tp.extended<ndt::fixed_dim_type>()->get_fixed_stride(dst_arrmeta);

        const array &identity = reinterpret_cast<data_type *>(data)->identity;

        const ndt::type &dst_element_tp = dst_tp.extended<ndt::fixed_dim_type>()->get_element_type();
        const char *dst_element_arrmeta = dst_arrmeta + sizeof(size_stride_t);

        intptr_t root_ckb_offset = ckb->size();
        ckb->emplace_back<reduction_kernel>(kernreq, dst_stride, src_stride);
        reduction_kernel *self_k = ckb->get_at<reduction_kernel>(root_ckb_offset);

        // The striding parameters
        self_k->_size = src_size;

        // Need to retrieve 'e' again because it may have moved
        if (identity.is_null()) {
          self_k->size_first = self_k->_size - 1;
          self_k->dst_stride_first = self_k->dst_stride;
          self_k->src_stride_first = self_k->src_stride;
        }
        else {
          self_k->size_first = self_k->_size;
          self_k->dst_stride_first = 0;
          self_k->src_stride_first = 0;
        }

        --reinterpret_cast<data_type *>(data)->ndim;

        self->instantiate(nullptr, data, ckb, dst_element_tp, dst_element_arrmeta, nsrc, &src0_element_tp, &src0_element_arrmeta,
                          kernel_request_strided, nkwd, kwds, tp_vars);
        self_k = reinterpret_cast<kernel_builder *>(ckb)->get_at<reduction_kernel>(root_ckb_offset);
        self_k->dst_init_kernel_offset = reinterpret_cast<data_type *>(data)->init_offset - root_ckb_offset;

        delete reinterpret_cast<data_type *>(data);
      }
    };

  } // namespace dynd::nd::functional
} // namespace dynd::nd
} // namespace dynd

/*
      if (!(data->naxis == 1 ||
            (static_data->properties & left_associative && static_data->properties & commutative))) {
        throw std::runtime_error("make_lifted_reduction_ckernel: for reducing "
                                 "along multiple dimensions,"
                                 " the reduction function must be both "
                                 "associative and commutative");
      }
      if (static_data->properties & right_associative) {
        throw std::runtime_error("make_lifted_reduction_ckernel: right_associative is "
                                 "not yet supported");
      }
*/
