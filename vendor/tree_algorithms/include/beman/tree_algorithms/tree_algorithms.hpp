// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_TREE_ALGORITHMS_TREE_ALGORITHMS_HPP
#define BEMAN_TREE_ALGORITHMS_TREE_ALGORITHMS_HPP

#include <beman/tree_algorithms/config.hpp>

#if BEMAN_TREE_ALGORITHMS_USE_MODULES() && !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

import beman.tree_algorithms;

#else

    #include <beman/tree_algorithms/binary_tree.hpp>
    #include <beman/tree_algorithms/box.hpp>
    #include <beman/tree_algorithms/child_slot.hpp>
    #include <beman/tree_algorithms/expression.hpp>
    #include <beman/tree_algorithms/fix.hpp>
    #include <beman/tree_algorithms/fold_map.hpp>
    #include <beman/tree_algorithms/fold_map_lookup.hpp>
    #include <beman/tree_algorithms/fringe_tree.hpp>
    #include <beman/tree_algorithms/functor.hpp>
    #include <beman/tree_algorithms/functors.hpp>
    #include <beman/tree_algorithms/overloaded.hpp>
    #include <beman/tree_algorithms/recursion_schemes.hpp>
    #include <beman/tree_algorithms/recursion_schemes_lookup.hpp>
    #include <beman/tree_algorithms/rose_tree.hpp>
    #include <beman/tree_algorithms/todo.hpp>

#endif // BEMAN_TREE_ALGORITHMS_USE_MODULES() &&
       // !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_TREE_ALGORITHMS_TREE_ALGORITHMS_HPP
