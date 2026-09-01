// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_TREE_ALGORITHMS_OVERLOADED_HPP
#define BEMAN_TREE_ALGORITHMS_OVERLOADED_HPP

#include <beman/tree_algorithms/config.hpp>

#if BEMAN_TREE_ALGORITHMS_USE_MODULES() && !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

import beman.tree_algorithms;

#else

// overloaded<Ts...> — aggregate visitor for std::visit.
//
// Usage:
//   std::visit(overloaded{
//       [](int x)         { ... },
//       [](std::string s) { ... },
//   }, v);
//
// The consteval catch-all fires a static_assert at compile time if std::visit
// encounters an alternative not covered by the explicit cases.  This turns
// variant exhaustiveness into a hard compile error rather than a silent
// default/no-op.  Adding a new alternative to a variant without handling it
// everywhere is caught immediately.
//
// No explicit deduction guide is needed: C++20 CTAD for aggregates deduces
// overloaded<F1, F2, ...> from the constructor arguments.

namespace beman::tree_algorithms {

template <typename... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;

    consteval void operator()(auto) const {
        static_assert(false, "overloaded: unhandled variant alternative — add a case");
    }
};

} // namespace beman::tree_algorithms

#endif // BEMAN_TREE_ALGORITHMS_USE_MODULES() &&
       // !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_TREE_ALGORITHMS_OVERLOADED_HPP
