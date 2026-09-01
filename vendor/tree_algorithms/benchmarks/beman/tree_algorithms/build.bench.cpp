// benchmarks/beman/tree_algorithms/build.bench.cpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Runtime cost of *producing* and *copying* a large binary tree, and the
// payoff of fusing a build into the fold that consumes it.
//
// Three questions, three sections:
//
//   build    unfold_fix grows a balanced Fix tree of N nodes from a seed.
//            The baseline is a hand-written recursive builder of the same
//            tree; the naive unique_ptr and std::indirect trees are built
//            the same way for comparison. All do N allocations of one node
//            each; the verb should not cost more than the loop it replaces.
//
//   copy     Here the representations genuinely differ, and honestly so.
//            Copying the Fix tree deep-copies every node (Box has value
//            semantics — Decision 6), which is O(N); so does the
//            std::indirect tree, for the same reason (indirect is Box's
//            standard analog). Copying the shared_ptr tree bumps one
//            refcount, O(1). The unique_ptr tree cannot be copied at all —
//            it is move-only, so a copy is a compile error, which is the
//            concrete face of "irregular type". This is the price/shape of
//            each ownership choice, not a price of the Fix operator; the
//            fold benchmarks show reads cost the same either way.
//
//   refold   refold computes fold(build(seed)) without ever materializing
//            the tree. Against unfold_fix-then-fold_fix it should win by
//            the whole allocation bill: same arithmetic, no spine built.
//            This is the answer to "isn't the intermediate tree wasteful" —
//            when you do not need the tree, you do not build it.

// The library's child_slot storage makes every F<Seed> layer a coalgebra
// produces inline (seeds are complete types), so unfold_fix allocates
// only the product tree's own nodes and refold allocates nothing at all.
// The (explicit inline control) rows re-derive the same behavior with a
// hand-written InlineLayer fmap (inline_layer.hpp) — the experiment that
// motivated child_slot, kept as a parity check on the registered path.

#include <beman/tree_algorithms/binary_tree.hpp>

#include "fix_unique_tree.hpp"
#include "inline_layer.hpp"
#include "naive_trees.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

using beman::tree_algorithms::BinaryTree;
using beman::tree_algorithms::BinaryTreeF;
using beman::tree_algorithms::BinaryTreeFix;
using beman::tree_algorithms::BinaryTreeLayer;
using beman::tree_algorithms::Box;
using beman::tree_algorithms::child_slot_t;
using beman::tree_algorithms::fold_fix;
using beman::tree_algorithms::make_box;
using beman::tree_algorithms::make_slot;
using beman::tree_algorithms::refold;
using beman::tree_algorithms::unfold_fix;

namespace {

// Nodes in the tree under test. Build and refold allocate on every
// sample, so this is deliberately smaller than the fold benchmark's tree;
// 2^16 - 1 nodes still exercises allocation and recursion at scale.
constexpr std::size_t kBuildNodes = (std::size_t{1} << 16) - 1;

using Layer = BinaryTreeLayer<std::int64_t>;
using Fixed = BinaryTreeFix<std::int64_t>;
using Range = std::pair<std::size_t, std::size_t>;

// Coalgebra: split [lo, hi) at its midpoint into a value and two child
// ranges. The value is the midpoint index, so a whole tree sums to
// 0 + 1 + ... + (N-1) and the answer is checkable. Range is complete, so
// the seed layer's child slots are inline — no allocation here.
constexpr auto coalgebra = [](const Range& r) -> BinaryTreeF<std::int64_t, Range> {
    auto [lo, hi]   = r;
    std::size_t mid = lo + (hi - lo) / 2;
    return BinaryTreeF<std::int64_t, Range>{static_cast<std::int64_t>(mid),
                                            mid > lo ? make_slot<Range>(Range{lo, mid}) : child_slot_t<Range>{},
                                            hi > mid + 1 ? make_slot<Range>(Range{mid + 1, hi})
                                                         : child_slot_t<Range>{}};
};

constexpr auto sum_layer = [](const BinaryTreeF<std::int64_t, std::int64_t>& layer) -> std::int64_t {
    std::int64_t total = layer.value;
    if (layer.left) {
        total += *layer.left;
    }
    if (layer.right) {
        total += *layer.right;
    }
    return total;
};

// The hand-written equivalent of unfold_fix over the same range split:
// the builder the verb is meant to replace.
auto build_hand(const Range& r) -> Fixed {
    auto [lo, hi]                        = r;
    std::size_t                      mid = lo + (hi - lo) / 2;
    BinaryTreeF<std::int64_t, Fixed> layer{static_cast<std::int64_t>(mid), Box<Fixed>{}, Box<Fixed>{}};
    if (mid > lo) {
        layer.left = make_box<Fixed>(build_hand(Range{lo, mid}));
    }
    if (hi > mid + 1) {
        layer.right = make_box<Fixed>(build_hand(Range{mid + 1, hi}));
    }
    return beman::tree_algorithms::wrap_fix<Layer::template F>(std::move(layer));
}

auto build_unfold(const Range& r) -> Fixed { return unfold_fix<Layer::template F>(coalgebra, r); }

auto refold_sum(const Range& r) -> std::int64_t {
    return refold<std::int64_t, Layer::template F>(sum_layer, coalgebra, r);
}

// The same coalgebra with the seed layer's children inline: no boxes just
// to hand two Ranges to the next recursion step.
constexpr auto coalgebra_inline = [](const Range& r) -> bench::InlineLayer<std::int64_t, Range> {
    auto [lo, hi]   = r;
    std::size_t mid = lo + (hi - lo) / 2;
    return bench::InlineLayer<std::int64_t, Range>{
        static_cast<std::int64_t>(mid),
        mid > lo ? std::optional<Range>(Range{lo, mid}) : std::optional<Range>{},
        hi > mid + 1 ? std::optional<Range>(Range{mid + 1, hi}) : std::optional<Range>{}};
};

// fmap from an inline seed layer into the boxed tree layer: only the real
// tree children are boxed — the allocation that IS the product.
struct InlineToBoxedFMap {
    template <typename Fn>
    auto operator()(Fn&& fn, const bench::InlineLayer<std::int64_t, Range>& layer) const {
        return BinaryTreeF<std::int64_t, Fixed>{
            layer.value,
            layer.left ? make_box<Fixed>(fn(*layer.left)) : Box<Fixed>{},
            layer.right ? make_box<Fixed>(fn(*layer.right)) : Box<Fixed>{},
        };
    }
};

auto build_unfold_inline(const Range& r) -> Fixed {
    return unfold_fix<Layer::template F>(coalgebra_inline, InlineToBoxedFMap{}, r);
}

constexpr auto inline_sum_layer = [](const bench::InlineLayer<std::int64_t, std::int64_t>& layer) -> std::int64_t {
    std::int64_t total = layer.value;
    if (layer.left) {
        total += *layer.left;
    }
    if (layer.right) {
        total += *layer.right;
    }
    return total;
};

// refold with inline layers everywhere: coalgebra emits an inline seed
// layer, fmap materializes an inline result layer, the algebra consumes
// it. Zero heap traffic in the whole computation.
template <typename A>
using InlineRangeLayer = bench::InlineLayer<std::int64_t, A>;

auto refold_sum_inline(const Range& r) -> std::int64_t {
    return refold<std::int64_t, InlineRangeLayer>(inline_sum_layer, coalgebra_inline, bench::inline_layer_fmap, r);
}

// Hand-written builders for the naive representations over the same range
// split (types and sum() in naive_trees.hpp).
auto build_unique(const Range& r) -> std::unique_ptr<bench::UniqueTree> {
    auto [lo, hi]    = r;
    std::size_t mid  = lo + (hi - lo) / 2;
    auto        node = std::make_unique<bench::UniqueTree>();
    node->value      = static_cast<std::int64_t>(mid);
    if (mid > lo) {
        node->left = build_unique(Range{lo, mid});
    }
    if (hi > mid + 1) {
        node->right = build_unique(Range{mid + 1, hi});
    }
    return node;
}

#ifdef __cpp_lib_indirect
auto build_indirect(const Range& r) -> bench::IndirectTree {
    auto [lo, hi]           = r;
    std::size_t         mid = lo + (hi - lo) / 2;
    bench::IndirectTree node;
    node.value = static_cast<std::int64_t>(mid);
    if (mid > lo) {
        node.left.emplace(build_indirect(Range{lo, mid}));
    }
    if (hi > mid + 1) {
        node.right.emplace(build_indirect(Range{mid + 1, hi}));
    }
    return node;
}
#endif // __cpp_lib_indirect

// A Fix tree over the unique_ptr-storage functor, grown over the same range
// split. Same Fix, same wrap_fix; only the functor's child storage differs.
using UFix = bench::UniquePtrTreeFix<std::int64_t>;

auto build_unique_fix(const Range& r) -> UFix {
    auto [lo, hi]                                 = r;
    std::size_t                               mid = lo + (hi - lo) / 2;
    bench::UniquePtrTreeF<std::int64_t, UFix> layer;
    layer.value = static_cast<std::int64_t>(mid);
    if (mid > lo) {
        layer.left = std::make_unique<UFix>(build_unique_fix(Range{lo, mid}));
    }
    if (hi > mid + 1) {
        layer.right = std::make_unique<UFix>(build_unique_fix(Range{mid + 1, hi}));
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

TEST_CASE("build: header wired up", "[build][bootstrap]") { REQUIRE(true); }

TEST_CASE("build: grow a large tree", "[build][!benchmark]") {
    const Range whole{0, kBuildNodes};

    // Law first: both builders produce a tree that folds to the same sum.
    const std::int64_t expected =
        static_cast<std::int64_t>(kBuildNodes) * (static_cast<std::int64_t>(kBuildNodes) - 1) / 2;
    REQUIRE(fold_fix<std::int64_t>(sum_layer, build_hand(whole)) == expected);
    REQUIRE(fold_fix<std::int64_t>(sum_layer, build_unfold(whole)) == expected);
    REQUIRE(fold_fix<std::int64_t>(sum_layer, build_unfold_inline(whole)) == expected);
    REQUIRE(sum_unique_fix(build_unique_fix(whole)) == expected);
    REQUIRE(bench::sum(*build_unique(whole)) == expected);
#ifdef __cpp_lib_indirect
    REQUIRE(bench::sum(build_indirect(whole)) == expected);
#endif

    BENCHMARK("hand recursive build (Fix<Box>)") { return build_hand(whole); };
    BENCHMARK("unfold_fix build (Fix<Box>)") { return build_unfold(whole); };
    BENCHMARK("unfold_fix build (explicit inline control)") { return build_unfold_inline(whole); };
    BENCHMARK("hand recursive build (Fix<unique_ptr>)") { return build_unique_fix(whole); };
    BENCHMARK("hand recursive build (unique_ptr)") { return build_unique(whole); };
#ifdef __cpp_lib_indirect
    BENCHMARK("hand recursive build (std::indirect)") { return build_indirect(whole); };
#endif
}

TEST_CASE("copy: value semantics vs structural sharing", "[build][copy][!benchmark]") {
    std::int64_t next = 0;
    auto         make = [&](auto&& self, int depth) -> BinaryTree<std::int64_t> {
        std::int64_t value = next++;
        if (depth <= 1) {
            return BinaryTree<std::int64_t>::leaf(value);
        }
        auto left  = self(self, depth - 1);
        auto right = self(self, depth - 1);
        return BinaryTree<std::int64_t>::node(value, std::move(left), std::move(right));
    };
    auto native = make(make, 16); // 2^16 - 1 nodes
    auto fixed  = beman::tree_algorithms::to_fix(native);

    // Regularity is a storage-policy property, not a Fix property, and it is
    // compile-checked here: the naive unique_ptr tree AND the Fix tree over
    // the unique_ptr-storage functor are both move-only, so "copy the tree"
    // is not an operation that exists for either — there is no copy row for
    // them below because the benchmark would not compile. Meanwhile Fix over
    // the Box functor is copyable, exactly like the Box vs unique_ptr choice
    // in a hand-written tree. Fix inherits whichever the functor chose.
    static_assert(!std::is_copy_constructible_v<bench::UniqueTree>,
                  "unique_ptr tree is move-only — no copy benchmark, and that is the point");
    static_assert(!std::is_copy_constructible_v<UFix>,
                  "Fix over the unique_ptr functor is move-only too — storage, not Fix, decides regularity");
    static_assert(std::is_copy_constructible_v<Fixed>, "Fix over the Box functor is regular (copyable)");

    // The honest asymmetry: O(1) refcount bump vs O(N) deep copy. std::indirect
    // deep-copies like Box, for the same value-semantic reason.
    BENCHMARK("copy shared_ptr tree (O(1) share)") { return native; };
    BENCHMARK("copy Fix<Box> tree (O(N) deep copy)") { return fixed; };
#ifdef __cpp_lib_indirect
    const Range whole{0, kBuildNodes};
    auto        indirect = build_indirect(whole);
    BENCHMARK("copy std::indirect tree (O(N) deep copy)") { return indirect; };
#endif
}

TEST_CASE("refold: fuse build into fold", "[build][refold][!benchmark]") {
    const Range        whole{0, kBuildNodes};
    const std::int64_t expected =
        static_cast<std::int64_t>(kBuildNodes) * (static_cast<std::int64_t>(kBuildNodes) - 1) / 2;

    REQUIRE(refold_sum(whole) == expected);
    REQUIRE(refold_sum_inline(whole) == expected);
    REQUIRE(fold_fix<std::int64_t>(sum_layer, build_unfold(whole)) == expected);

    BENCHMARK("unfold_fix then fold_fix (tree materialized)") {
        return fold_fix<std::int64_t>(sum_layer, build_unfold(whole));
    };
    BENCHMARK("refold (fused, no tree)") { return refold_sum(whole); };
    BENCHMARK("refold (explicit inline control)") { return refold_sum_inline(whole); };
}
