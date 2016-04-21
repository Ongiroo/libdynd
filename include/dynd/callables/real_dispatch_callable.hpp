//
// Copyright (C) 2011-16 DyND Developers
// BSD 2-Clause License, see LICENSE.txt
//

#pragma once

#include <dynd/callables/base_dispatch_callable.hpp>

namespace dynd {
namespace nd {

  class real_dispatch_callable : public base_dispatch_callable {
    dispatcher<1, callable> m_dispatcher;

  public:
    real_dispatch_callable(const ndt::type &tp, const dispatcher<1, callable> &dispatcher)
        : base_dispatch_callable(tp), m_dispatcher(dispatcher) {}

    const callable &specialize(const ndt::type &DYND_UNUSED(dst_tp), intptr_t DYND_UNUSED(nsrc),
                               const ndt::type *src_tp) {
      return m_dispatcher(src_tp[0].get_id());
    }
  };

} // namespace dynd::nd
} // namespace dynd
