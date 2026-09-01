// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_TODO_HPP
#define BEMAN_SPECGEN_TODO_HPP

#include <beman/specgen/config.hpp>

#if BEMAN_SPECGEN_USE_MODULES() && !defined(BEMAN_SPECGEN_INCLUDED_FROM_INTERFACE_UNIT)

import beman.specgen;

#else

namespace beman::specgen {

// TODO

} // namespace beman::specgen

#endif // BEMAN_SPECGEN_USE_MODULES() &&
       // !defined(BEMAN_SPECGEN_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_SPECGEN_TODO_HPP
