// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/tree_algorithms/fringe_tree.hpp>
#include <beman/tree_algorithms/fringe_tree.hpp> // Re-inclusion: verifies include guard

#include <beman/tree_algorithms/fold_map.hpp>
#include <beman/tree_algorithms/recursion_schemes.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <variant>
#include <vector>

using beman::tree_algorithms::fold_map;
using beman::tree_algorithms::fold_with;
using beman::tree_algorithms::fringe_tree_embed;
using beman::tree_algorithms::fringe_tree_layer_fold_map;
using beman::tree_algorithms::fringe_tree_project;
using beman::tree_algorithms::FringeBranch;
using beman::tree_algorithms::FringeEmpty;
using beman::tree_algorithms::FringeLeaf;
using beman::tree_algorithms::FringeTree;
using beman::tree_algorithms::FringeTreeF;
using beman::tree_algorithms::functor_typeclass;
using beman::tree_algorithms::has_functor_instance;
using beman::tree_algorithms::overloaded;
using beman::tree_algorithms::unfold_with;
using beman::tree_algorithms::view_l;
using beman::tree_algorithms::view_r;

namespace {

using Tree = FringeTree<int>;
using Ptr  = const Tree*;

// Order-and-shape-pinning algebra over one fringe layer: leaves render
// their value, branches parenthesize. Distinguishes both element order
// and tree shape (Decision 7 / DEV-01).
inline auto shape_algebra = [](const FringeTreeF<int, std::string>& layer) -> std::string {
    return std::visit(overloaded{
                          [](const FringeEmpty&) -> std::string { return "()"; },
                          [](const FringeLeaf<int>& l) { return std::to_string(l.value); },
                          [](const FringeBranch<std::string>& b) { return "(" + b.left + " " + b.right + ")"; },
                      },
                      layer);
};

inline auto fringe_shape(const Tree& t) -> std::string {
    const auto& fmap_fn = [](auto&& fn, const auto& layer) {
        using Layer = std::remove_cvref_t<decltype(layer)>;
        return functor_typeclass<Layer>.fmap(std::forward<decltype(fn)>(fn), layer);
    };
    return fold_with<std::string>(shape_algebra, fmap_fn, fringe_tree_project, static_cast<Ptr>(&t));
}

// ---------------------------------------------------------------------
// Constexpr coverage (DEV-04) for the layer machinery. The tree itself
// is shared_ptr-based and runtime-only; the layer, its Functor
// instance, and the layer fold are fully constexpr.
// ---------------------------------------------------------------------

static_assert(has_functor_instance<FringeTreeF<int, int>>);

constexpr auto fmap_doubles_branch_handles() -> bool {
    FringeTreeF<int, int> layer{FringeBranch<int>{3, 4}};
    auto        doubled = functor_typeclass<FringeTreeF<int, int>>.fmap([](const int& x) { return x * 2; }, layer);
    const auto* b       = std::get_if<FringeBranch<int>>(&doubled);
    return b != nullptr && b->left == 6 && b->right == 8;
}

static_assert(fmap_doubles_branch_handles());

constexpr auto layer_fold_covers_all_alternatives() -> bool {
    auto map_fn  = [](int x) { return x + 1; };
    auto combine = [](int a, int b) { return a * 10 + b; }; // order probe only, per-layer
    bool empty_is_identity =
        fringe_tree_layer_fold_map(map_fn, combine, -7, FringeTreeF<int, int>{FringeEmpty{}}) == -7;
    bool leaf_maps = fringe_tree_layer_fold_map(map_fn, combine, -7, FringeTreeF<int, int>{FringeLeaf<int>{4}}) == 5;
    bool branch_combines_in_order =
        fringe_tree_layer_fold_map(map_fn, combine, -7, FringeTreeF<int, int>{FringeBranch<int>{1, 2}}) == 12;
    return empty_is_identity && leaf_maps && branch_combines_in_order;
}

static_assert(layer_fold_covers_all_alternatives());

} // namespace

// ---------------------------------------------------------------------
// The ported persistent tree behaves as before.
// ---------------------------------------------------------------------

TEST_CASE("FringeTree - Constructors", "[tree_algorithms::fringe_tree]") {
    auto e = Tree::empty();
    CHECK(e.is_empty());
    CHECK(e.measure() == 0U);

    auto l = Tree::leaf(7);
    CHECK(l.is_leaf());
    CHECK(l.value() == 7);
    CHECK(l.measure() == 1U);

    auto b = Tree::branch(Tree::leaf(1), Tree::leaf(2));
    CHECK(b.is_branch());
    CHECK(b.measure() == 2U);
    CHECK(b.left().value() == 1);
    CHECK(b.right().value() == 2);
}

TEST_CASE("FringeTree - ConcatEmptyIsIdentity", "[tree_algorithms::fringe_tree]") {
    auto t = Tree::branch(Tree::leaf(1), Tree::leaf(2));
    CHECK(Tree::concat(Tree::empty(), t).flatten() == std::vector<int>{1, 2});
    CHECK(Tree::concat(t, Tree::empty()).flatten() == std::vector<int>{1, 2});
    // And concat with empty does not add a branch level.
    CHECK(Tree::concat(Tree::empty(), t).measure() == 2U);
}

TEST_CASE("FringeTree - SequenceOrder", "[tree_algorithms::fringe_tree]") {
    auto t = Tree::from_sequence({1, 2, 3});
    CHECK(t.flatten() == std::vector<int>{1, 2, 3});
    CHECK(t.measure() == 3U);

    // snoc chains left-lean, cons chains right-lean; flatten order is the
    // sequence either way.
    auto c = Tree::leaf(3).cons(2).cons(1);
    CHECK(c.flatten() == std::vector<int>{1, 2, 3});
}

TEST_CASE("FringeTree - ViewsDecomposeOnTheQuotient", "[tree_algorithms::fringe_tree]") {
    // The same sequence in two shapes: snoc-built leans left, cons-built
    // leans right. The view contract is a quotient contract — the same
    // end element comes off, and the rest is specified as a SEQUENCE;
    // its shape is the representation's own business.
    auto left_leaning  = Tree::from_sequence({1, 2, 3});
    auto right_leaning = Tree::leaf(3).cons(2).cons(1);

    auto vl1 = view_l(left_leaning);
    auto vl2 = view_l(right_leaning);
    REQUIRE(vl1);
    REQUIRE(vl2);
    CHECK(vl1->d_value == 1);
    CHECK(vl2->d_value == 1);
    CHECK(vl1->d_rest.flatten() == std::vector<int>{2, 3});
    CHECK(vl2->d_rest.flatten() == std::vector<int>{2, 3});
    // The cached measure survives the decomposition (the rest is built
    // through concat/branch, so it is correct by construction).
    CHECK(vl1->d_rest.measure() == 2U);
    CHECK(vl2->d_rest.measure() == 2U);

    auto vr = view_r(left_leaning);
    REQUIRE(vr);
    CHECK(vr->d_value == 3);
    CHECK(vr->d_rest.flatten() == std::vector<int>{1, 2});

    // Boundary cases: empty yields nothing; a leaf yields its value and
    // the empty rest.
    CHECK(!view_l(Tree::empty()));
    CHECK(!view_r(Tree::empty()));
    auto vleaf = view_r(Tree::leaf(9));
    REQUIRE(vleaf);
    CHECK(vleaf->d_value == 9);
    CHECK(vleaf->d_rest.is_empty());
}

// ---------------------------------------------------------------------
// Direct verbs over the fringe tree: no Fix, no Box, no conversion.
// ---------------------------------------------------------------------

TEST_CASE("FringeTree - DirectFoldPinsShapeAndOrder", "[tree_algorithms::fringe_tree]") {
    // from_sequence({1,2,3}) snocs, so the shape leans left; the cons
    // chain leans right. Same sequence, different — and observable —
    // shapes.
    CHECK(fringe_shape(Tree::from_sequence({1, 2, 3})) == "((1 2) 3)");
    CHECK(fringe_shape(Tree::leaf(3).cons(2).cons(1)) == "(1 (2 3))");
    CHECK(fringe_shape(Tree::from_sequence({3, 2, 1})) == "((3 2) 1)");
    CHECK(fringe_shape(Tree::empty()) == "()");
}

TEST_CASE("FringeTree - UnfoldWithMaintainsMeasureInvariant", "[tree_algorithms::fringe_tree]") {
    // Build a balanced tree over [0, 8) directly into the shared_ptr
    // representation. The embedding routes every branch through
    // FringeTree::branch(), so every cached measure is correct by
    // construction — checked here at the root and one level down, and
    // against flatten().
    using Range          = std::pair<int, int>;
    auto split_coalgebra = [](const Range& r) -> FringeTreeF<int, Range> {
        auto [lo, hi] = r;
        if (hi - lo <= 0) {
            return FringeEmpty{};
        }
        if (hi - lo == 1) {
            return FringeLeaf<int>{lo};
        }
        int mid = lo + (hi - lo) / 2;
        return FringeBranch<Range>{Range{lo, mid}, Range{mid, hi}};
    };
    const auto& fmap_fn = [](auto&& fn, const auto& layer) {
        using Layer = std::remove_cvref_t<decltype(layer)>;
        return functor_typeclass<Layer>.fmap(std::forward<decltype(fn)>(fn), layer);
    };

    auto tree = unfold_with<Tree>(split_coalgebra, fmap_fn, fringe_tree_embed, Range{0, 8});

    CHECK(tree.flatten() == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7});
    CHECK(tree.measure() == 8U);
    REQUIRE(tree.is_branch());
    CHECK(tree.left().measure() == 4U);
    CHECK(tree.right().measure() == 4U);
    CHECK(fringe_shape(tree) == "(((0 1) (2 3)) ((4 5) (6 7)))");
}

// ---------------------------------------------------------------------
// fold_map over the fringe tree.
// ---------------------------------------------------------------------

TEST_CASE("FringeTree - FoldMapConcatFollowsTheSequence", "[tree_algorithms::fringe_tree]") {
    const auto& fmap_fn = [](auto&& fn, const auto& layer) {
        using Layer = std::remove_cvref_t<decltype(layer)>;
        return functor_typeclass<Layer>.fmap(std::forward<decltype(fn)>(fn), layer);
    };
    auto concat = [](const std::string& a, const std::string& b) { return a + b; };
    auto show   = [](int x) { return std::to_string(x); };

    auto run = [&](const Tree& t) {
        return fold_map<std::string>(show,
                                     concat,
                                     std::string{},
                                     fringe_tree_layer_fold_map,
                                     fmap_fn,
                                     fringe_tree_project,
                                     static_cast<Ptr>(&t));
    };

    // DEV-01: the non-commutative monoid observes element order...
    CHECK(run(Tree::from_sequence({1, 2, 3})) == "123");
    CHECK(run(Tree::from_sequence({3, 2, 1})) == "321");

    // ...while associativity makes it shape-independent: the left-leaning
    // snoc chain and the right-leaning cons chain hold the same sequence
    // and fold to the same string. That is the monoid laws paying rent —
    // shape may rebalance freely without changing any fold_map.
    CHECK(run(Tree::leaf(3).cons(2).cons(1)) == "123");

    // Derived queries, one-liners as ever.
    auto plus = [](int a, int b) { return a + b; };
    auto t    = Tree::from_sequence({1, 2, 3, 4, 5});
    CHECK(fold_map<int>([](int) { return 1; },
                        plus,
                        0,
                        fringe_tree_layer_fold_map,
                        fmap_fn,
                        fringe_tree_project,
                        static_cast<Ptr>(&t)) == 5);
    CHECK(fold_map<int>([](int x) { return x; },
                        plus,
                        0,
                        fringe_tree_layer_fold_map,
                        fmap_fn,
                        fringe_tree_project,
                        static_cast<Ptr>(&t)) == 15);
}
