// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/tree_algorithms/rose_tree.hpp>
#include <beman/tree_algorithms/rose_tree.hpp> // Re-inclusion: verifies include guard

#include <beman/tree_algorithms/binary_tree.hpp>
#include <beman/tree_algorithms/fold_map.hpp>
#include <beman/tree_algorithms/fold_map_lookup.hpp>
#include <beman/tree_algorithms/recursion_schemes_lookup.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

using beman::tree_algorithms::BinaryTreeF;
using beman::tree_algorithms::BinaryTreeFix;
using beman::tree_algorithms::BinaryTreeLayer;
using beman::tree_algorithms::Box;
using beman::tree_algorithms::fold_fix;
using beman::tree_algorithms::fold_map;
using beman::tree_algorithms::functor_typeclass;
using beman::tree_algorithms::has_functor_instance;
using beman::tree_algorithms::make_box;
using beman::tree_algorithms::rose;
using beman::tree_algorithms::rose_layer_fold_map;
using beman::tree_algorithms::RoseF;
using beman::tree_algorithms::RoseLayer;
using beman::tree_algorithms::RoseTreeFix;
using beman::tree_algorithms::unfold_fix;
using beman::tree_algorithms::wrap_fix;

namespace {

using Tree = RoseTreeFix<int>;

// Order-and-shape-pinning algebra over one rose layer: a leaf renders
// its value, an internal node parenthesizes value-then-children. Both
// element order and nesting are observable (Decision 7 / DEV-01).
inline auto shape_algebra = [](const RoseF<int, std::string>& layer) -> std::string {
    if (layer.children.empty()) {
        return std::to_string(layer.value);
    }
    std::string out = "(" + std::to_string(layer.value);
    for (const std::string& child : layer.children) {
        out += " " + child;
    }
    return out + ")";
};

inline auto rose_shape(const Tree& t) -> std::string { return fold_fix<std::string>(shape_algebra, t); }

// ---------------------------------------------------------------------
// Constexpr coverage (DEV-04). The rose tree is Fix-native and vector is
// constexpr-capable in C++23 for transient allocations, so the whole
// lifecycle — build, fold — evaluates at compile time.
// ---------------------------------------------------------------------

static_assert(has_functor_instance<RoseF<int, int>>);

constexpr auto fmap_maps_children_in_order() -> bool {
    RoseF<int, int> layer{7, {1, 2, 3}};
    auto            mapped = functor_typeclass<RoseF<int, int>>.fmap([](const int& x) { return x * 2; }, layer);
    return mapped.value == 7 && mapped.children == std::vector<int>{2, 4, 6};
}

static_assert(fmap_maps_children_in_order());

constexpr auto layer_fold_is_preorder() -> bool {
    auto map_fn  = [](int x) { return x + 1; };
    auto combine = [](int a, int b) { return a * 10 + b; }; // order probe only, per-layer
    // Value first (mapped), then the already-folded child results, left
    // to right: map(1)=2, then 2, then 3.
    bool preorder = rose_layer_fold_map(map_fn, combine, -7, RoseF<int, int>{1, {2, 3}}) == 223;
    // A leaf is a node with no children: just its mapped value; the
    // identity never contributes.
    bool leaf = rose_layer_fold_map(map_fn, combine, -7, RoseF<int, int>{4, {}}) == 5;
    return preorder && leaf;
}

static_assert(layer_fold_is_preorder());

constexpr auto whole_lifecycle_at_compile_time() -> bool {
    Tree t       = rose<int>(1, {rose<int>(2), rose<int>(3, {rose<int>(4)})});
    auto combine = [](int a, int b) { return a * 10 + b; };
    // (1 (2) (3 (4))) pre-order under the digit probe: 3→34, then
    // 1→12→(12*10+34).
    return fold_map<int>([](int x) { return x; }, combine, 0, t) == 154;
}

static_assert(whole_lifecycle_at_compile_time());

} // namespace

// ---------------------------------------------------------------------
// The verbs over the Fix-native rose tree.
// ---------------------------------------------------------------------

TEST_CASE("RoseTree - FoldPinsShapeAndOrder", "[tree_algorithms::rose_tree]") {
    auto t = rose<int>(1, {rose<int>(2), rose<int>(3, {rose<int>(4)}), rose<int>(5)});
    CHECK(rose_shape(t) == "(1 2 (3 4) 5)");

    // Reordered children are a different tree and render differently.
    auto swapped = rose<int>(1, {rose<int>(3, {rose<int>(4)}), rose<int>(2), rose<int>(5)});
    CHECK(rose_shape(swapped) == "(1 (3 4) 2 5)");
}

TEST_CASE("RoseTree - UnfoldGrowsFanout", "[tree_algorithms::rose_tree]") {
    // Seed (label, fanout): a node numbers its children label*10+i and
    // shrinks the fanout — labels pin both order and depth.
    using Seed     = std::pair<int, int>;
    auto coalgebra = [](const Seed& s) -> RoseF<int, Seed> {
        auto [label, fanout] = s;
        std::vector<Seed> children;
        children.reserve(static_cast<std::size_t>(fanout));
        for (int i = 1; i <= fanout; ++i) {
            children.push_back(Seed{label * 10 + i, fanout - 1});
        }
        return RoseF<int, Seed>{label, std::move(children)};
    };

    auto t = unfold_fix<RoseLayer<int>::template F>(coalgebra, Seed{1, 2});
    CHECK(rose_shape(t) == "(1 (11 111) (12 121))");
}

// ---------------------------------------------------------------------
// fold_map over the rose tree, and the equivalence law pinning the
// lookup tier to the explicit tier.
// ---------------------------------------------------------------------

TEST_CASE("RoseTree - FoldMapPreorderContract", "[tree_algorithms::rose_tree]") {
    auto t = rose<int>(1, {rose<int>(2), rose<int>(3, {rose<int>(4)}), rose<int>(5)});

    auto show   = [](int x) { return std::to_string(x); };
    auto concat = [](const std::string& a, const std::string& b) { return a + b; };

    // DEV-01: string concatenation is non-commutative, so the pre-order
    // contract is observed, not assumed.
    CHECK(fold_map<std::string>(show, concat, std::string{}, t) == "12345");

    auto swapped = rose<int>(1, {rose<int>(3, {rose<int>(4)}), rose<int>(2), rose<int>(5)});
    CHECK(fold_map<std::string>(show, concat, std::string{}, swapped) == "13425");

    // Equivalence law: the lookup tier IS the explicit tier with the
    // registered ingredients — same answer through both spellings.
    const auto& fmap_fn = [](auto&& fn, const auto& layer) {
        using Layer = std::remove_cvref_t<decltype(layer)>;
        return functor_typeclass<Layer>.fmap(std::forward<decltype(fn)>(fn), layer);
    };
    CHECK(fold_map<std::string>(show, concat, std::string{}, rose_layer_fold_map, fmap_fn, t) ==
          fold_map<std::string>(show, concat, std::string{}, t));

    // Derived one-liners.
    auto plus = [](int a, int b) { return a + b; };
    CHECK(fold_map<int>([](int) { return 1; }, plus, 0, t) == 5);
    CHECK(fold_map<int>([](int x) { return x; }, plus, 0, t) == 15);
}

// ---------------------------------------------------------------------
// There is no generic in-order: the same fold_map call, two
// representations, two contractual orders.
// ---------------------------------------------------------------------

TEST_CASE("RoseTree - InOrderIsBinaryNotGeneric", "[tree_algorithms::rose_tree]") {
    auto show   = [](int x) { return std::to_string(x); };
    auto concat = [](const std::string& a, const std::string& b) { return a + b; };

    // The same three values, 2 above 1 and 3, in both representations.
    using BinTree = BinaryTreeFix<int>;
    auto bin_leaf = [](int v) -> BinTree {
        return wrap_fix<BinaryTreeLayer<int>::template F>(
            BinaryTreeF<int, BinTree>{v, Box<BinTree>{}, Box<BinTree>{}});
    };
    auto bin = wrap_fix<BinaryTreeLayer<int>::template F>(
        BinaryTreeF<int, BinTree>{2, make_box<BinTree>(bin_leaf(1)), make_box<BinTree>(bin_leaf(3))});

    auto rosy = rose<int>(2, {rose<int>(1), rose<int>(3)});

    // The binary layer's contract is in-order — the value sits between
    // its two children. The rose layer has no such slot (which child
    // pair would the value sit between?), so its contract is pre-order.
    // Neither is wrong, and no generic in-order exists to impose.
    CHECK(fold_map<std::string>(show, concat, std::string{}, bin) == "123");
    CHECK(fold_map<std::string>(show, concat, std::string{}, rosy) == "213");
}
