// benchmarks/beman/tree_algorithms/naive_trees.hpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef BEMAN_TREE_ALGORITHMS_BENCH_NAIVE_TREES_HPP
#define BEMAN_TREE_ALGORITHMS_BENCH_NAIVE_TREES_HPP

// Hand-written "naive" binary trees used as runtime baselines against the
// Fix machinery: a value at every node, optional children, no library
// involved. They differ only in the owning edge to a child — exactly the
// axis under study:
//
//   UniqueTree    std::unique_ptr child: nullable and cheap, but move-only,
//                 so the tree type is NOT regular (uncopyable). That
//                 irregularity is the point of including it.
//   IndirectTree  std::optional<std::indirect<>> child (C++26): the
//                 standard's value-semantic indirection — regular and
//                 deep-copying, the standard analog of this library's Box.
//                 An engaged optional-of-indirect is exactly Box's "nullable
//                 owning value". Present only where the toolchain provides
//                 std::indirect (__cpp_lib_indirect), e.g. g++-16 at
//                 -std=gnu++26.
//
// sum() is provided here (the read-only traversal is identical regardless of
// the edge type); each benchmark supplies its own builder, since they grow
// trees differently (by depth vs. by range split).

#include <cstdint>
#include <memory>
#include <optional>
#if __has_include(<indirect>)
    #include <indirect>
#endif

namespace bench {

struct UniqueTree {
    std::int64_t                value;
    std::unique_ptr<UniqueTree> left;
    std::unique_ptr<UniqueTree> right;
};

inline auto sum(const UniqueTree& tree) -> std::int64_t {
    std::int64_t total = tree.value;
    if (tree.left) {
        total += sum(*tree.left);
    }
    if (tree.right) {
        total += sum(*tree.right);
    }
    return total;
}

#ifdef __cpp_lib_indirect
struct IndirectTree {
    std::int64_t                               value;
    std::optional<std::indirect<IndirectTree>> left;
    std::optional<std::indirect<IndirectTree>> right;
};

inline auto sum(const IndirectTree& tree) -> std::int64_t {
    std::int64_t total = tree.value;
    if (tree.left) {
        total += sum(**tree.left);
    }
    if (tree.right) {
        total += sum(**tree.right);
    }
    return total;
}
#endif // __cpp_lib_indirect

} // namespace bench

#endif // BEMAN_TREE_ALGORITHMS_BENCH_NAIVE_TREES_HPP
