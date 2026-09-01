// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/tree_algorithms/fold_map.hpp>
#include <beman/tree_algorithms/fold_map.hpp> // Re-inclusion: verifies include guard

#include <beman/tree_algorithms/binary_tree.hpp>
#include <beman/tree_algorithms/box.hpp>
#include <beman/tree_algorithms/expression.hpp>
#include <beman/tree_algorithms/fold_map_lookup.hpp>
#include <beman/tree_algorithms/fringe_tree.hpp>
#include <beman/tree_algorithms/overloaded.hpp>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>
#include <utility>
#include <variant>

using beman::tree_algorithms::add_node;
using beman::tree_algorithms::binary_tree_layer_fold_map;
using beman::tree_algorithms::BinaryTree;
using beman::tree_algorithms::BinaryTreeF;
using beman::tree_algorithms::BinaryTreeFix;
using beman::tree_algorithms::BinaryTreeLayer;
using beman::tree_algorithms::Box;
using beman::tree_algorithms::child_slot_t;
using beman::tree_algorithms::const_node;
using beman::tree_algorithms::eval_algebra;
using beman::tree_algorithms::Fix;
using beman::tree_algorithms::fold_fix;
using beman::tree_algorithms::fold_map;
using beman::tree_algorithms::FringeTree;
using beman::tree_algorithms::has_layer_fold_instance;
using beman::tree_algorithms::has_project_instance;
using beman::tree_algorithms::make_box;
using beman::tree_algorithms::make_slot;
using beman::tree_algorithms::mul_node;
using beman::tree_algorithms::overloaded;
using beman::tree_algorithms::to_fix;
using beman::tree_algorithms::wrap_fix;

namespace bta = beman::tree_algorithms;

namespace {

// ---------------------------------------------------------------------
// IntListF — cons-lists of int; the elements are the heads, in cons
// order. Mirrors the fixture in recursion_schemes.test.cpp.
// ---------------------------------------------------------------------

struct Nil {};

template <typename A>
struct Cons {
    int    head;
    Box<A> tail;
};

template <typename A>
using IntListF = std::variant<Nil, Cons<A>>;

using IntList = Fix<IntListF>;

template <typename A, typename F>
constexpr auto fmap_list(F&& f, const IntListF<A>& list) {
    using B = std::remove_cvref_t<std::invoke_result_t<F, const A&>>;
    return std::visit(
        overloaded{
            [](const Nil&) -> IntListF<B> { return Nil{}; },
            [&f](const Cons<A>& c) -> IntListF<B> { return Cons<B>{c.head, make_box<B>(std::invoke(f, *c.tail))}; },
        },
        list);
}

inline constexpr auto fmap_list_fn = [](auto&& f, const auto& list) {
    return fmap_list(std::forward<decltype(f)>(f), list);
};

constexpr auto make_nil() -> IntList { return wrap_fix<IntListF>(IntListF<IntList>{Nil{}}); }

constexpr auto make_cons(int head, IntList tail) -> IntList {
    return wrap_fix<IntListF>(IntListF<IntList>{Cons<IntList>{head, make_box<IntList>(std::move(tail))}});
}

// The layer fold for IntListF: Nil is the identity; Cons combines the
// mapped head with the already-folded tail, head first (cons order).
struct ListLayerFoldMap {
    template <typename MapFn, typename Combine, typename Result>
    constexpr auto operator()(const MapFn&            map_fn,
                              const Combine&          combine,
                              const Result&           identity,
                              const IntListF<Result>& layer) const -> Result {
        return std::visit(overloaded{
                              [&](const Nil&) -> Result { return identity; },
                              [&](const Cons<Result>& c) -> Result { return combine(map_fn(c.head), *c.tail); },
                          },
                          layer);
    }
};

inline constexpr ListLayerFoldMap list_layer_fold_map{};

// ---------------------------------------------------------------------
// Constexpr coverage (DEV-04) with the canonical DEV-01 non-commutative
// monoid: strings under concatenation (associative, "" is a two-sided
// identity — a combine like a*10+b fails both laws, and the leaf
// positions, which combine against the identity, expose it).
// std::string is constexpr-capable for transient use; std::to_string is
// not constexpr in C++23, so the map builds one-char strings directly.
// ---------------------------------------------------------------------

inline constexpr auto show_digit   = [](int x) { return std::string(1, static_cast<char>('0' + x)); };
inline constexpr auto concat       = [](const std::string& a, const std::string& b) { return a + b; };
inline constexpr auto identity_map = [](int x) { return x; };

constexpr auto list_concat_in_order() -> bool {
    auto list = make_cons(1, make_cons(2, make_cons(3, make_nil())));
    return fold_map<std::string>(show_digit, concat, std::string{}, list_layer_fold_map, fmap_list_fn, list) == "123";
}

static_assert(list_concat_in_order());

// The derivation law, at compile time: fold_map IS fold_fix with the
// monoid algebra spelled by hand.
constexpr auto fold_map_matches_handwritten_algebra() -> bool {
    auto list = make_cons(7, make_cons(8, make_cons(9, make_nil())));
    auto by_fold_map =
        fold_map<std::string>(show_digit, concat, std::string{}, list_layer_fold_map, fmap_list_fn, list);
    auto algebra = [](const IntListF<std::string>& layer) -> std::string {
        return std::visit(overloaded{
                              [](const Nil&) -> std::string { return {}; },
                              [](const Cons<std::string>& c) { return concat(show_digit(c.head), *c.tail); },
                          },
                          layer);
    };
    return by_fold_map == fold_fix<std::string>(algebra, fmap_list_fn, list);
}

static_assert(fold_map_matches_handwritten_algebra());

// Binary tree, in-order concatenation: node(1, leaf 2, leaf 3) folds as
// combine(combine("2", "1"), "3") = "213". The mirrored tree gives
// "312", so the in-order contract is observable in the value.
using IntFixed = BinaryTreeFix<int>;

constexpr auto fixed_leaf(int v) -> IntFixed {
    return wrap_fix<BinaryTreeLayer<int>::F>(BinaryTreeF<int, IntFixed>{v, Box<IntFixed>{}, Box<IntFixed>{}});
}

constexpr auto fixed_node(int v, IntFixed l, IntFixed r) -> IntFixed {
    return wrap_fix<BinaryTreeLayer<int>::F>(
        BinaryTreeF<int, IntFixed>{v, make_box<IntFixed>(std::move(l)), make_box<IntFixed>(std::move(r))});
}

template <typename A, typename Fn>
constexpr auto fmap_btree(Fn&& fn, const BinaryTreeF<int, A>& layer) {
    using B = std::remove_cvref_t<std::invoke_result_t<Fn, const A&>>;
    return BinaryTreeF<int, B>{
        layer.value,
        layer.left ? make_slot<B>(std::invoke(fn, *layer.left)) : child_slot_t<B>{},
        layer.right ? make_slot<B>(std::invoke(fn, *layer.right)) : child_slot_t<B>{},
    };
}

inline constexpr auto fmap_btree_fn = [](auto&& fn, const auto& layer) {
    return fmap_btree(std::forward<decltype(fn)>(fn), layer);
};

constexpr auto tree_concat_in_order() -> bool {
    auto tree = fixed_node(1, fixed_leaf(2), fixed_leaf(3));
    return fold_map<std::string>(show_digit, concat, std::string{}, binary_tree_layer_fold_map, fmap_btree_fn, tree) ==
           "213";
}

constexpr auto tree_concat_mirrored() -> bool {
    auto mirrored = fixed_node(1, fixed_leaf(3), fixed_leaf(2));
    return fold_map<std::string>(
               show_digit, concat, std::string{}, binary_tree_layer_fold_map, fmap_btree_fn, mirrored) == "312";
}

static_assert(tree_concat_in_order());
static_assert(tree_concat_mirrored());

// ---------------------------------------------------------------------
// Lookup tier (fold_map_lookup.hpp): the everyday four-argument
// spelling, resolving layer fold / fmap / projection through the
// typeclass objects registered in the adapter headers.
// ---------------------------------------------------------------------

static_assert(has_layer_fold_instance<BinaryTreeF<int, int>>);
static_assert(has_layer_fold_instance<bta::ExprF<int>>);
static_assert(has_project_instance<BinaryTree<int>>);
static_assert(has_project_instance<FringeTree<int>>);
static_assert(!has_layer_fold_instance<int>);
static_assert(!has_project_instance<int>);

// The elementwise/structural contrast on the expression tree: summing
// the constants of (1 + 2) * 3 sees the elements (6); evaluating it
// interprets the structure (9).
constexpr auto expr_constants_vs_eval() -> bool {
    auto expr = mul_node(add_node(const_node(1), const_node(2)), const_node(3));
    auto plus = [](int a, int b) { return a + b; };

    int constant_sum   = fold_map<int>(identity_map, plus, 0, expr);
    int constant_count = fold_map<int>([](int) { return 1; }, plus, 0, expr);
    int evaluated      = bta::fold_fix<int>(eval_algebra, expr);
    return constant_sum == 6 && constant_count == 3 && evaluated == 9;
}

static_assert(expr_constants_vs_eval());

// Lookup overload agrees with the explicit form, at compile time.
constexpr auto lookup_matches_explicit() -> bool {
    auto tree       = fixed_node(1, fixed_leaf(2), fixed_leaf(3));
    auto via_lookup = fold_map<std::string>(show_digit, concat, std::string{}, tree);
    auto via_explicit =
        fold_map<std::string>(show_digit, concat, std::string{}, binary_tree_layer_fold_map, fmap_btree_fn, tree);
    return via_lookup == via_explicit && via_lookup == "213";
}

static_assert(lookup_matches_explicit());

} // namespace

// ---------------------------------------------------------------------
// DEV-01: the non-commutative monoid of strings under concatenation.
// ---------------------------------------------------------------------

TEST_CASE("FoldMap - ListStringConcatPinsConsOrder", "[tree_algorithms::fold_map]") {
    auto list   = make_cons(1, make_cons(2, make_cons(3, make_nil())));
    auto concat = [](const std::string& a, const std::string& b) { return a + b; };
    auto show   = [](int x) { return std::to_string(x); };

    CHECK(fold_map<std::string>(show, concat, std::string{}, list_layer_fold_map, fmap_list_fn, list) == "123");
}

TEST_CASE("FoldMap - TreeStringConcatPinsInOrder", "[tree_algorithms::fold_map]") {
    // In-order over node(1, node(2, leaf 4, leaf 5), leaf 3) visits
    // 4 2 5 1 3; the mirrored tree visits 3 1 5 2 4.
    auto concat = [](const std::string& a, const std::string& b) { return a + b; };
    auto show   = [](int x) { return std::to_string(x); };

    auto tree = fixed_node(1, fixed_node(2, fixed_leaf(4), fixed_leaf(5)), fixed_leaf(3));
    CHECK(fold_map<std::string>(show, concat, std::string{}, binary_tree_layer_fold_map, fmap_btree_fn, tree) ==
          "42513");

    auto mirrored = fixed_node(1, fixed_leaf(3), fixed_node(2, fixed_leaf(5), fixed_leaf(4)));
    CHECK(fold_map<std::string>(show, concat, std::string{}, binary_tree_layer_fold_map, fmap_btree_fn, mirrored) ==
          "31524");
}

TEST_CASE("FoldMap - DerivedQueriesFromOneHook", "[tree_algorithms::fold_map]") {
    // The classic derived operations are one-liner fold_maps: length via
    // map-to-one under addition, sum via the identity map, any_of via
    // map-to-bool under logical-or.
    auto tree = fixed_node(1, fixed_node(2, fixed_leaf(4), fixed_leaf(5)), fixed_leaf(3));

    auto plus = [](int a, int b) { return a + b; };
    CHECK(fold_map<int>([](int) { return 1; }, plus, 0, binary_tree_layer_fold_map, fmap_btree_fn, tree) == 5);
    CHECK(fold_map<int>(identity_map, plus, 0, binary_tree_layer_fold_map, fmap_btree_fn, tree) == 15);

    auto lor = [](bool a, bool b) { return a || b; };
    CHECK(fold_map<bool>([](int x) { return x > 4; }, lor, false, binary_tree_layer_fold_map, fmap_btree_fn, tree));
    CHECK_FALSE(
        fold_map<bool>([](int x) { return x > 5; }, lor, false, binary_tree_layer_fold_map, fmap_btree_fn, tree));
}

TEST_CASE("FoldMap - LookupOverloadOverTreesInTheirOwnRepresentation", "[tree_algorithms::fold_map]") {
    // The everyday spelling: elementwise pieces plus the tree, everything
    // structural resolved through the registered typeclass objects. Works
    // identically over both shipped representations.
    auto show_s   = [](int x) { return std::to_string(x); };
    auto concat_s = [](const std::string& a, const std::string& b) { return a + b; };

    auto bt = BinaryTree<int>::node(
        1, BinaryTree<int>::node(2, BinaryTree<int>::leaf(4), BinaryTree<int>::leaf(5)), BinaryTree<int>::leaf(3));
    CHECK(fold_map<std::string>(show_s, concat_s, std::string{}, bt) == "42513");

    auto ft = FringeTree<int>::from_sequence({1, 2, 3});
    CHECK(fold_map<std::string>(show_s, concat_s, std::string{}, ft) == "123");

    auto plus = [](int a, int b) { return a + b; };
    CHECK(fold_map<int>([](int) { return 1; }, plus, 0, bt) == 5);
    CHECK(fold_map<int>([](int) { return 1; }, plus, 0, ft) == 3);
}

TEST_CASE("FoldMap - DirectPathAgreesWithAdapterPath", "[tree_algorithms::fold_map]") {
    // The same shared_ptr tree, elementwise-folded two ways: through
    // to_fix + the Fix overload, and directly through a projection with
    // the seven-parameter overload. Same layer_fold, same fmap.
    using IntTree = BinaryTree<int>;
    using Ptr     = const IntTree*;
    auto project  = [](Ptr t) -> BinaryTreeF<int, Ptr> {
        return BinaryTreeF<int, Ptr>{t->value(),
                                     t->has_left() ? make_slot<Ptr>(&t->left()) : child_slot_t<Ptr>{},
                                     t->has_right() ? make_slot<Ptr>(&t->right()) : child_slot_t<Ptr>{}};
    };

    auto t      = IntTree::node(1, IntTree::node(2, IntTree::leaf(4), IntTree::leaf(5)), IntTree::leaf(3));
    auto concat = [](const std::string& a, const std::string& b) { return a + b; };
    auto show   = [](int x) { return std::to_string(x); };

    auto direct = fold_map<std::string>(
        show, concat, std::string{}, binary_tree_layer_fold_map, fmap_btree_fn, project, static_cast<Ptr>(&t));
    auto adapted =
        fold_map<std::string>(show, concat, std::string{}, binary_tree_layer_fold_map, fmap_btree_fn, to_fix(t));

    CHECK(direct == adapted);
    CHECK(direct == "42513");
}
