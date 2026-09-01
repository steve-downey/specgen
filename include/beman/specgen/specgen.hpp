// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_SPECGEN_HPP
#define BEMAN_SPECGEN_SPECGEN_HPP

#include <beman/specgen/config.hpp>

#if BEMAN_SPECGEN_USE_MODULES() && !defined(BEMAN_SPECGEN_INCLUDED_FROM_INTERFACE_UNIT)

import beman.specgen;

#else

    #include <beman/specgen/todo.hpp>

#endif // BEMAN_SPECGEN_USE_MODULES() &&
       // !defined(BEMAN_SPECGEN_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_SPECGEN_SPECGEN_HPP
