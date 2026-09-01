// benchmarks/beman/tree_algorithms/fold.bench.cpp                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Runtime cost of *consuming* a very large binary tree.
//
// The claim under test: the Fix operator, though expressive, is somehow
// too expensive at runtime. Folding is the honest place to look — it is
// the operation every representation must pay in full, visiting every
// node once. If Fix imposes a tax, a whole-tree fold is where it shows.
//
// One tree of ~2^kFoldDepth nodes is built once, in three shapes that
// share the same values, and summed four ways:
//
//   hand           plain hand-written recursion over the shared_ptr tree
//                  (the "what you would have written anyway" baseline)
//   fold_map/native the direct verb over the shared_ptr tree in its own
//                  representation, via the registered projection — no Fix
//                  is ever materialized
//   fold_map/fix    the elementwise fold over the Fix tree
//   fold_fix/fix    the raw algebra fold over the Fix tree
//
// A fold does not copy the spine; it chases child pointers and combines.
// Box (Fix's child edge) and shared_ptr (the native tree's child edge)
// both dereference to the child in O(1); the atomic refcount that makes
// shared_ptr copies expensive is never touched on a read-only traversal.
// So the expectation this benchmark is here to confirm is that the numbers
// sit on top of one another: Fix costs what hand recursion costs.
//
// Two extra "naive" hand-written representations widen the field, so Fix is
// not only compared against the shared_ptr tree:
//
//   unique_ptr     nullable, move-only children. Cheap, but the tree type
//                  is not copyable — an irregular type. (Traversal is
//                  unaffected; regularity only bites on copy.)
//   std::indirect  the standard's value-semantic indirection (C++26), the
//                  closest analog to this library's Box: regular, deep-copy.
//                  It is not nullable, so an absent child is
//                  std::optional<std::indirect<...>> — exactly Box's
//                  "nullable owning value". Compiled in only where the
//                  toolchain provides it (__cpp_lib_indirect), e.g. g++-16
//                  at -std=gnu++26.

// The library's layers hold children in child_slot storage: Box exactly
// at the knot, inline std::optional at complete types, so the F<Result>
// layers a fold materializes allocate nothing. The (explicit inline
// control) rows run the same verbs with a hand-written fmap over
// InlineLayer (inline_layer.hpp) — the experiment that motivated the
// child_slot adoption, kept as a parity check: the registered path
// should match it, and both should sit on "hand recursion (Fix<Box>
// tree)", the floor for this tree layout.

#include <beman/tree_algorithms/binary_tree.hpp>

#include "fix_unique_tree.hpp"
#include "inline_layer.hpp"
#include "naive_trees.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

using beman::tree_algorithms::BinaryTree;
using beman::tree_algorithms::BinaryTreeF;
using beman::tree_algorithms::BinaryTreeFix;
using beman::tree_algorithms::fold_fix;
using beman::tree_algorithms::fold_map;
using beman::tree_algorithms::fold_with;
using beman::tree_algorithms::to_fix;
using beman::tree_algorithms::unwrap_fix;

namespace {

// Depth of the complete tree under test. 20 => 2^20 - 1 ≈ 1.05M nodes,
// ~100-200 MB per representation once built. Both representations are
// built once, outside the timed region. Bump for a bigger tree.
constexpr int kFoldDepth = 20;

// A complete binary tree of the given depth, values numbered 1..N in a
// deterministic order so the fold has a checkable answer.
auto build_native(int depth, std::int64_t& next) -> BinaryTree<std::int64_t> {
    std::int64_t value = next++;
    if (depth <= 1) {
        return BinaryTree<std::int64_t>::leaf(value);
    }
    auto left  = build_native(depth - 1, next);
    auto right = build_native(depth - 1, next);
    return BinaryTree<std::int64_t>::node(value, std::move(left), std::move(right));
}

// Hand-written recursion: the baseline every other number is measured
// against. Walks the shared_ptr spine, no library machinery.
auto sum_hand(const BinaryTree<std::int64_t>& tree) -> std::int64_t {
    std::int64_t total = tree.value();
    if (tree.has_left()) {
        total += sum_hand(tree.left());
    }
    if (tree.has_right()) {
        total += sum_hand(tree.right());
    }
    return total;
}

// Builders for the naive representations (types and sum() live in
// naive_trees.hpp). Same complete shape as build_native, value-per-node
// numbered in the same preorder — so every representation folds to the same
// total.
auto build_unique(int depth, std::int64_t& next) -> std::unique_ptr<bench::UniqueTree> {
    auto node   = std::make_unique<bench::UniqueTree>();
    node->value = next++;
    if (depth > 1) {
        node->left  = build_unique(depth - 1, next);
        node->right = build_unique(depth - 1, next);
    }
    return node;
}

#ifdef __cpp_lib_indirect
auto build_indirect(int depth, std::int64_t& next) -> bench::IndirectTree {
    bench::IndirectTree node;
    node.value = next++;
    if (depth > 1) {
        node.left.emplace(build_indirect(depth - 1, next));
        node.right.emplace(build_indirect(depth - 1, next));
    }
    return node;
}
#endif // __cpp_lib_indirect

constexpr auto add = [](std::int64_t a, std::int64_t b) -> std::int64_t { return a + b; };
constexpr auto id  = [](std::int64_t v) -> std::int64_t { return v; };

// Direct verb over the native tree: fold_map resolves the projection and
// layer fold through the registered typeclasses; no Fix is materialized.
auto sum_direct(const BinaryTree<std::int64_t>& tree) -> std::int64_t {
    return fold_map<std::int64_t>(id, add, std::int64_t{0}, tree);
}

// Elementwise fold over the Fix tree.
auto sum_fix_map(const BinaryTreeFix<std::int64_t>& tree) -> std::int64_t {
    return fold_map<std::int64_t>(id, add, std::int64_t{0}, tree);
}

// Raw algebra fold over the Fix tree: the algebra sees one already-folded
// layer and combines it.
auto sum_fix_algebra(const BinaryTreeFix<std::int64_t>& tree) -> std::int64_t {
    auto algebra = [](const BinaryTreeF<std::int64_t, std::int64_t>& layer) -> std::int64_t {
        std::int64_t total = layer.value;
        if (layer.left) {
            total += *layer.left;
        }
        if (layer.right) {
            total += *layer.right;
        }
        return total;
    };
    return fold_fix<std::int64_t>(algebra, tree);
}

// The floor: hand recursion over the Fix<Box> tree itself. Same node
// layout as every fold_fix row (value + two owning pointers); no verb, no
// materialized layer. Any gap between this and a fold_fix row is verb
// overhead, not tree layout.
auto sum_fix_hand(const BinaryTreeFix<std::int64_t>& tree) -> std::int64_t {
    const auto&  layer = unwrap_fix(tree);
    std::int64_t total = layer.value;
    if (layer.left.ptr) {
        total += sum_fix_hand(*layer.left);
    }
    if (layer.right.ptr) {
        total += sum_fix_hand(*layer.right);
    }
    return total;
}

// The explicit inline-result fmap for BinaryTreeF layers: the experiment
// that motivated child_slot, kept as a parity control for the registered
// fmap (InlineLayer ≅ BinaryTreeF at a complete type; both allocate
// nothing).
struct BoxedToInlineFMap {
    template <typename Fn>
    auto operator()(Fn&& fn, const BinaryTreeF<std::int64_t, BinaryTreeFix<std::int64_t> >& layer) const {
        using B = std::remove_cvref_t<std::invoke_result_t<Fn&, const BinaryTreeFix<std::int64_t>&> >;
        return bench::InlineLayer<std::int64_t, B>{
            layer.value,
            layer.left ? std::optional<B>(fn(*layer.left)) : std::optional<B>{},
            layer.right ? std::optional<B>(fn(*layer.right)) : std::optional<B>{},
        };
    }
};

constexpr auto inline_sum_algebra = [](const bench::InlineLayer<std::int64_t, std::int64_t>& layer) -> std::int64_t {
    std::int64_t total = layer.value;
    if (layer.left) {
        total += *layer.left;
    }
    if (layer.right) {
        total += *layer.right;
    }
    return total;
};

// Same tree, same fold_fix verb (the explicit-fmap overload), only the
// materialized layer's storage differs: inline instead of boxed.
auto sum_fix_inline(const BinaryTreeFix<std::int64_t>& tree) -> std::int64_t {
    return fold_fix<std::int64_t>(inline_sum_algebra, BoxedToInlineFMap{}, tree);
}

// The direct verb with inline layers end to end: the projection exposes a
// layer of the shared_ptr tree with inline pointer handles, and the fmap
// materializes inline result layers. No allocation anywhere in the fold —
// against sum_direct (registered projection, boxed handles AND boxed
// results), this isolates what the direct verb's boxing costs.
auto sum_direct_inline(const BinaryTree<std::int64_t>& tree) -> std::int64_t {
    using P      = const BinaryTree<std::int64_t>*;
    auto project = [](P node) {
        return bench::InlineLayer<std::int64_t, P>{
            node->value(),
            node->has_left() ? std::optional<P>(&node->left()) : std::optional<P>{},
            node->has_right() ? std::optional<P>(&node->right()) : std::optional<P>{},
        };
    };
    return fold_with<std::int64_t>(inline_sum_algebra, bench::inline_layer_fmap, project, &tree);
}

// A Fix tree over the unique_ptr-storage functor: same Fix, same fold_fix,
// only the functor's child storage differs (unique_ptr, not Box). Built with
// the same preorder numbering as build_native so it folds to the same total.
using UFix = bench::UniquePtrTreeFix<std::int64_t>;

auto build_unique_fix(int depth, std::int64_t& next) -> UFix {
    bench::UniquePtrTreeF<std::int64_t, UFix> layer;
    layer.value = next++;
    if (depth > 1) {
        layer.left  = std::make_unique<UFix>(build_unique_fix(depth - 1, next));
        layer.right = std::make_unique<UFix>(build_unique_fix(depth - 1, next));
    }
    return beman::tree_algorithms::wrap_fix<bench::UniquePtrTreeLayer<std::int64_t>::template F>(std::move(layer));
}

auto sum_unique_fix(const UFix& tree) -> std::int64_t {
    auto algebra = [](const bench::UniquePtrTreeF<std::int64_t, std::int64_t>& layer) -> std::int64_t {
        std::int64_t total = layer.value;
        if (layer.left) {
            total += *layer.left;
        }
        if (layer.right) {
            total += *layer.right;
        }
        return total;
    };
    return fold_fix<std::int64_t>(algebra, tree);
}

} // namespace

TEST_CASE("fold: header wired up", "[fold][bootstrap]") {
    // Bootstrap: a green run means the file compiled, linked, and the
    // benchmark harness is present.
    REQUIRE(true);
}

TEST_CASE("fold: sum a very large tree", "[fold][!benchmark]") {
    std::int64_t next   = 1;
    auto         native = build_native(kFoldDepth, next);
    auto         fixed  = to_fix(native);

    const std::int64_t nodes    = next - 1;
    const std::int64_t expected = nodes * (nodes + 1) / 2;

    // The naive representations, each built to the same shape (the per-node
    // numbering differs but the total does not).
    std::int64_t un         = 1;
    auto         unique_    = build_unique(kFoldDepth, un);
    std::int64_t uf         = 1;
    auto         unique_fix = build_unique_fix(kFoldDepth, uf);
#ifdef __cpp_lib_indirect
    std::int64_t in       = 1;
    auto         indirect = build_indirect(kFoldDepth, in);
#endif

    // Law first: every route computes the same sum before any timing.
    REQUIRE(sum_hand(native) == expected);
    REQUIRE(bench::sum(*unique_) == expected);
    REQUIRE(sum_unique_fix(unique_fix) == expected);
    REQUIRE(sum_direct(native) == expected);
    REQUIRE(sum_direct_inline(native) == expected);
    REQUIRE(sum_fix_map(fixed) == expected);
    REQUIRE(sum_fix_algebra(fixed) == expected);
    REQUIRE(sum_fix_inline(fixed) == expected);
    REQUIRE(sum_fix_hand(fixed) == expected);
#ifdef __cpp_lib_indirect
    REQUIRE(bench::sum(indirect) == expected);
#endif

    BENCHMARK("hand recursion (shared_ptr)") { return sum_hand(native); };
    BENCHMARK("hand recursion (unique_ptr)") { return bench::sum(*unique_); };
#ifdef __cpp_lib_indirect
    BENCHMARK("hand recursion (std::indirect)") { return bench::sum(indirect); };
#endif
    BENCHMARK("hand recursion (Fix<Box> tree)") { return sum_fix_hand(fixed); };
    BENCHMARK("fold_map / native (direct verb)") { return sum_direct(native); };
    BENCHMARK("fold_with / native (explicit inline control)") { return sum_direct_inline(native); };
    BENCHMARK("fold_fix / Fix<Box>") { return sum_fix_algebra(fixed); };
    BENCHMARK("fold_fix / Fix<Box> (explicit inline control)") { return sum_fix_inline(fixed); };
    BENCHMARK("fold_map / Fix<Box>") { return sum_fix_map(fixed); };
    BENCHMARK("fold_fix / Fix<unique_ptr>") { return sum_unique_fix(unique_fix); };
}
