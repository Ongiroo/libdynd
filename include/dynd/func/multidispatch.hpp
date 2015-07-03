//
// Copyright (C) 2011-15 DyND Developers
// BSD 2-Clause License, see LICENSE.txt
//

#pragma once

#include <dynd/func/arrfunc.hpp>
#include <dynd/kernels/multidispatch_kernel.hpp>

namespace dynd {
namespace nd {
  namespace functional {

    /**
     * Creates a multiple dispatch arrfunc out of a set of arrfuncs. The
     * input arrfuncs must have concrete signatures.
     *
     * \param naf  The number of arrfuncs provided.
     * \param af  The array of input arrfuncs, sized ``naf``.
     */
    arrfunc multidispatch(intptr_t naf, const arrfunc *af);

    inline arrfunc multidispatch(const std::initializer_list<arrfunc> &children)
    {
      return multidispatch(children.size(), children.begin());
    }

    arrfunc multidispatch(const ndt::type &self_tp,
                          const std::vector<arrfunc> &children,
                          const std::vector<std::string> &ignore_vars);

    arrfunc multidispatch(const ndt::type &self_tp,
                          const std::vector<arrfunc> &children);

    arrfunc multidispatch(const ndt::type &self_tp, intptr_t size,
                          const arrfunc *children, const arrfunc &default_child,
                          bool own_children, intptr_t i0 = 0);

    arrfunc multidispatch_by_type_id(const ndt::type &self_tp,
                                     const std::vector<arrfunc> &children);

    inline arrfunc multidispatch_by_type_id(const ndt::type &self_tp,
                                            intptr_t size,
                                            const arrfunc *children,
                                            const arrfunc &default_child,
                                            bool own_children, intptr_t i0 = 0)
    {
      return multidispatch(self_tp, size, children, default_child, own_children,
                           i0);
    }

    template <int N0>
    arrfunc multidispatch(const ndt::type &self_tp,
                          const arrfunc (&children)[N0],
                          const arrfunc &default_child, intptr_t i0 = 0)
    {
      return multidispatch(self_tp, N0, children, default_child, false, i0);
    }

    template <int N0, int N1>
    arrfunc multidispatch(const ndt::type &self_tp,
                          const arrfunc (&children)[N0][N1],
                          const arrfunc &DYND_UNUSED(default_child),
                          const std::initializer_list<intptr_t> &i = {0, 1})
    {
      for (int i0 = 0; i0 < N0; ++i0) {
        for (int i1 = 0; i1 < N1; ++i1) {
          const arrfunc &child = children[i0][i1];
          if (!child.is_null()) {
            std::map<string, ndt::type> tp_vars;
            if (!self_tp.match(child.get_array_type(), tp_vars)) {
              throw std::invalid_argument("could not match arrfuncs");
            }
          }
        }
      }

      typedef typename multidispatch_kernel<arrfunc[N0][N1]>::static_data
          data_type;

      return arrfunc::make<multidispatch_kernel<arrfunc[N0][N1]>>(
          self_tp, data_type(children, i.begin()), 0);
    }

  } // namespace dynd::nd::functional
} // namespace dynd::nd
} // namespace dynd
