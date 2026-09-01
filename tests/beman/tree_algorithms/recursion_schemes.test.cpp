// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/tree_algorithms/recursion_schemes.hpp>
#include <beman/tree_algorithms/recursion_schemes.hpp> // Re-inclusion: verifies include guard

#include <beman/tree_algorithms/box.hpp>
#include <beman/tree_algorithms/overloaded.hpp>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

using beman::tree_algorithms::Box;
using beman::tree_algorithms::Fix;
using beman::tree_algorithms::fold_fix;
using beman::tree_algorithms::fold_with;
using beman::tree_algorithms::make_box;
using beman::tree_algorithms::overloaded;
using beman::tree_algorithms::refold;
using beman::tree_algorithms::unfold_fix;
using beman::tree_algorithms::unfold_with;
using beman::tree_algorithms::unwrap_fix;
using beman::tree_algorithms::wrap_fix;

namespace {

// ---------------------------------------------------------------------
// NatF — unary naturals (ported from the fixpoint suite).
// ---------------------------------------------------------------------

struct Zero {};

template <typename A>
struct Succ {
    Box<A> pred;
};

template <typename A>
using NatF = std::variant<Zero, Succ<A>>;

using Nat = Fix<NatF>;

constexpr auto make_zero() -> Nat { return wrap_fix<NatF>(NatF<Nat>{Zero{}}); }

constexpr auto make_succ(Nat n) -> Nat { return wrap_fix<NatF>(NatF<Nat>{Succ<Nat>{make_box<Nat>(std::move(n))}}); }

template <typename A, typename F>
constexpr auto fmap_nat(F&& f, const NatF<A>& nat) {
    using B = std::remove_cvref_t<std::invoke_result_t<F, const A&>>;
    return std::visit(overloaded{
                          [](const Zero&) -> NatF<B> { return Zero{}; },
                          [&f](const Succ<A>& s) -> NatF<B> { return Succ<B>{make_box<B>(std::invoke(f, *s.pred))}; },
                      },
                      nat);
}

inline constexpr auto fmap_nat_fn = [](auto&& f, const auto& nat) {
    return fmap_nat(std::forward<decltype(f)>(f), nat);
};

inline constexpr auto count_algebra = [](const NatF<int>& n) -> int {
    return std::visit(overloaded{
                          [](const Zero&) { return 0; },
                          [](const Succ<int>& s) { return *s.pred + 1; },
                      },
                      n);
};

inline constexpr auto nat_coalgebra = [](int n) -> NatF<int> {
    if (n <= 0)
        return Zero{};
    return Succ<int>{make_box<int>(n - 1)};
};

// ---------------------------------------------------------------------
// IntListF — cons-lists of int, for the DEV-01 non-idempotent algebras.
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

// ---------------------------------------------------------------------
// TreeF — external binary tree, payload at leaves, for order-sensitive
// (non-commutative) fold and structure-sensitive refold tests.
// ---------------------------------------------------------------------

struct Leaf {
    int value;
};

template <typename A>
struct Node {
    Box<A> left;
    Box<A> right;
};

template <typename A>
using TreeF = std::variant<Leaf, Node<A>>;

using IntTree = Fix<TreeF>;

template <typename A, typename F>
constexpr auto fmap_tree(F&& f, const TreeF<A>& tree) {
    using B = std::remove_cvref_t<std::invoke_result_t<F, const A&>>;
    return std::visit(overloaded{
                          [](const Leaf& l) -> TreeF<B> { return Leaf{l.value}; },
                          [&f](const Node<A>& n) -> TreeF<B> {
                              return Node<B>{make_box<B>(std::invoke(f, *n.left)),
                                             make_box<B>(std::invoke(f, *n.right))};
                          },
                      },
                      tree);
}

inline constexpr auto fmap_tree_fn = [](auto&& f, const auto& tree) {
    return fmap_tree(std::forward<decltype(f)>(f), tree);
};

constexpr auto make_leaf(int v) -> IntTree { return wrap_fix<TreeF>(TreeF<IntTree>{Leaf{v}}); }

constexpr auto make_node(IntTree l, IntTree r) -> IntTree {
    return wrap_fix<TreeF>(
        TreeF<IntTree>{Node<IntTree>{make_box<IntTree>(std::move(l)), make_box<IntTree>(std::move(r))}});
}

// Non-commutative numeric algebra: leaf -> value, node -> left - right.
// Subtraction is neither commutative nor associative, so swapped children
// or re-associated structure produce a different answer.
inline constexpr auto subtract_algebra = [](const TreeF<int>& layer) -> int {
    return std::visit(overloaded{
                          [](const Leaf& l) { return l.value; },
                          [](const Node<int>& n) { return *n.left - *n.right; },
                      },
                      layer);
};

// Position-marking string algebra: the exact parenthesization records the
// shape and child order of the tree.
inline auto show_algebra = [](const TreeF<std::string>& layer) -> std::string {
    return std::visit(overloaded{
                          [](const Leaf& l) { return std::to_string(l.value); },
                          [](const Node<std::string>& n) { return "(" + *n.left + "-" + *n.right + ")"; },
                      },
                      layer);
};

// Range-splitting coalgebra: seed [lo, hi) becomes Leaf{lo} when the range
// is a single element, otherwise a Node splitting at the midpoint. The
// produced structure is position-sensitive: mis-threading either child seed
// changes the parenthesization (and the subtraction result).
using Range = std::pair<int, int>;

inline constexpr auto split_coalgebra = [](const Range& r) -> TreeF<Range> {
    auto [lo, hi] = r;
    if (hi - lo <= 1)
        return Leaf{lo};
    int mid = lo + (hi - lo) / 2;
    return Node<Range>{make_box<Range>(Range{lo, mid}), make_box<Range>(Range{mid, hi})};
};

// ---------------------------------------------------------------------
// Constexpr coverage (DEV-04): at least one static_assert per verb, each
// using an order-sensitive computation.
// ---------------------------------------------------------------------

// fold_fix: ((1 - 2) - 3) == -4; any child swap or re-association differs.
constexpr auto fold_subtracts_in_order() -> int {
    auto tree = make_node(make_node(make_leaf(1), make_leaf(2)), make_leaf(3));
    return fold_fix<int>(subtract_algebra, fmap_tree_fn, tree);
}

static_assert(fold_subtracts_in_order() == -4);

// unfold_fix: countdown-of-squares list from 3 is [9, 4, 1]; folded with the
// non-commutative combine h + 2 * rest: 9 + 2*(4 + 2*(1 + 2*0)) == 21.
// Wrong seed threading or wrong depth changes both the list and the total.
constexpr auto unfold_squares_then_weigh() -> int {
    auto squares_coalgebra = [](int n) -> IntListF<int> {
        if (n <= 0)
            return Nil{};
        return Cons<int>{n * n, make_box<int>(n - 1)};
    };
    auto weigh_algebra = [](const IntListF<int>& layer) -> int {
        return std::visit(overloaded{
                              [](const Nil&) { return 0; },
                              [](const Cons<int>& c) { return c.head + 2 * *c.tail; },
                          },
                          layer);
    };
    auto list = unfold_fix<IntListF>(squares_coalgebra, fmap_list_fn, 3);
    return fold_fix<int>(weigh_algebra, fmap_list_fn, list);
}

static_assert(unfold_squares_then_weigh() == 21);

// refold: split [1, 4) -> Node(Leaf 1, Node(Leaf 2, Leaf 3)); subtraction
// gives 1 - (2 - 3) == 2. Wrong split threading or wrong fusion order
// changes the association and hence the value.
constexpr auto refold_split_subtract() -> int {
    return refold<int, TreeF>(subtract_algebra, split_coalgebra, fmap_tree_fn, Range{1, 4});
}

static_assert(refold_split_subtract() == 2);

// ---------------------------------------------------------------------
// Direct-verb equivalence laws (constexpr, DEV-04): fold_with with
// unwrap_fix as the projection IS fold_fix; unfold_with with wrap_fix as
// the embedding IS unfold_fix. Order-sensitive computations throughout.
// ---------------------------------------------------------------------

// fold_with(…, unwrap_fix, t) ≡ fold_fix(…, t) on the subtraction tree.
constexpr auto fold_with_unwrap_matches_fold_fix() -> bool {
    auto tree    = make_node(make_node(make_leaf(1), make_leaf(2)), make_leaf(3));
    auto project = [](const IntTree& t) -> const TreeF<IntTree>& { return unwrap_fix(t); };
    return fold_with<int>(subtract_algebra, fmap_tree_fn, project, tree) ==
           fold_fix<int>(subtract_algebra, fmap_tree_fn, tree);
}

static_assert(fold_with_unwrap_matches_fold_fix());

// unfold_with(…, wrap_fix, seed) ≡ unfold_fix(…, seed), compared through
// the position-sensitive squares spine from the unfold_fix test.
constexpr auto unfold_with_wrap_matches_unfold_fix() -> bool {
    auto squares_coalgebra = [](int n) -> IntListF<int> {
        if (n <= 0)
            return Nil{};
        return Cons<int>{n * n, make_box<int>(n - 1)};
    };
    auto weigh_algebra = [](const IntListF<int>& layer) -> int {
        return std::visit(overloaded{
                              [](const Nil&) { return 0; },
                              [](const Cons<int>& c) { return c.head + 2 * *c.tail; },
                          },
                          layer);
    };
    auto embed = [](IntListF<IntList>&& layer) -> IntList { return wrap_fix<IntListF>(std::move(layer)); };

    auto direct       = unfold_with<IntList>(squares_coalgebra, fmap_list_fn, embed, 3);
    auto materialized = unfold_fix<IntListF>(squares_coalgebra, fmap_list_fn, 3);
    return fold_fix<int>(weigh_algebra, fmap_list_fn, direct) ==
           fold_fix<int>(weigh_algebra, fmap_list_fn, materialized);
}

static_assert(unfold_with_wrap_matches_unfold_fix());

// ---------------------------------------------------------------------
// A nonce tree: the tree you wrote yesterday. unique_ptr children (not
// even copyable), no Fix, no Box, no typeclasses. The direct verbs run
// over it through a three-ingredient adapter: a base-functor layer, an
// fmap for that layer, and a projection handing out raw-pointer handles.
// ---------------------------------------------------------------------

struct NonceNode {
    int                        value;
    std::unique_ptr<NonceNode> left;  // null = absent child
    std::unique_ptr<NonceNode> right; // null = absent child
};

auto nonce_leaf(int v) -> std::unique_ptr<NonceNode> {
    return std::make_unique<NonceNode>(NonceNode{v, nullptr, nullptr});
}

auto nonce_node(int v, std::unique_ptr<NonceNode> l, std::unique_ptr<NonceNode> r) -> std::unique_ptr<NonceNode> {
    return std::make_unique<NonceNode>(NonceNode{v, std::move(l), std::move(r)});
}

// One layer of a NonceNode: the value plus optional child slots. The
// child slot type A is whatever handles the projection deals in.
template <typename A>
struct NonceF {
    int              value;
    std::optional<A> left;
    std::optional<A> right;
};

template <typename A, typename F>
auto fmap_nonce(F&& f, const NonceF<A>& layer) {
    using B = std::remove_cvref_t<std::invoke_result_t<F, const A&>>;
    return NonceF<B>{layer.value,
                     layer.left ? std::optional<B>{std::invoke(f, *layer.left)} : std::optional<B>{},
                     layer.right ? std::optional<B>{std::invoke(f, *layer.right)} : std::optional<B>{}};
}

inline constexpr auto fmap_nonce_fn = [](auto&& f, const auto& layer) {
    return fmap_nonce(std::forward<decltype(f)>(f), layer);
};

// Projection: one layer, children as raw non-owning pointers. The verbs
// recurse on exactly the handle type the projection accepts.
inline constexpr auto nonce_project = [](const NonceNode* n) -> NonceF<const NonceNode*> {
    return {n->value,
            n->left ? std::optional<const NonceNode*>{n->left.get()} : std::optional<const NonceNode*>{},
            n->right ? std::optional<const NonceNode*>{n->right.get()} : std::optional<const NonceNode*>{}};
};

// Order-sensitive shape algebra, as in the adapter example: "." marks an
// absent child, so shape, child order, and levels are all pinned.
inline auto nonce_shape_algebra = [](const NonceF<std::string>& layer) -> std::string {
    auto child = [](const std::optional<std::string>& c) { return c ? *c : std::string("."); };
    return "(" + child(layer.left) + " " + std::to_string(layer.value) + " " + child(layer.right) + ")";
};

} // namespace

// ---------------------------------------------------------------------
// Ported fixpoint suite (NatF).
// ---------------------------------------------------------------------

TEST_CASE("FoldFix - NatZero", "[tree_algorithms::recursion_schemes]") {
    auto zero = make_zero();
    CHECK(fold_fix<int>(count_algebra, fmap_nat_fn, zero) == 0);
}

TEST_CASE("FoldFix - NatTwo", "[tree_algorithms::recursion_schemes]") {
    auto two = make_succ(make_succ(make_zero()));
    CHECK(fold_fix<int>(count_algebra, fmap_nat_fn, two) == 2);
}

TEST_CASE("FoldFix - NatFive", "[tree_algorithms::recursion_schemes]") {
    auto n = make_zero();
    for (int i = 0; i < 5; ++i) {
        n = make_succ(std::move(n));
    }
    CHECK(fold_fix<int>(count_algebra, fmap_nat_fn, n) == 5);
}

TEST_CASE("FoldFix - NatCustomAlgebra", "[tree_algorithms::recursion_schemes]") {
    auto three = make_succ(make_succ(make_succ(make_zero())));

    auto bool_algebra = [](const NatF<bool>& n) -> bool {
        return std::visit(overloaded{
                              [](const Zero&) { return true; },
                              [](const Succ<bool>& s) { return !*s.pred; },
                          },
                          n);
    };

    CHECK(fold_fix<bool>(bool_algebra, fmap_nat_fn, three) == false);
}

TEST_CASE("UnfoldFix - NatFromZero", "[tree_algorithms::recursion_schemes]") {
    auto nat = unfold_fix<NatF>(nat_coalgebra, fmap_nat_fn, 0);
    CHECK(fold_fix<int>(count_algebra, fmap_nat_fn, nat) == 0);
}

TEST_CASE("UnfoldFix - NatFromFive", "[tree_algorithms::recursion_schemes]") {
    auto nat = unfold_fix<NatF>(nat_coalgebra, fmap_nat_fn, 5);
    CHECK(fold_fix<int>(count_algebra, fmap_nat_fn, nat) == 5);
}

TEST_CASE("Refold - NatZero", "[tree_algorithms::recursion_schemes]") {
    CHECK(refold<int, NatF>(count_algebra, nat_coalgebra, fmap_nat_fn, 0) == 0);
}

TEST_CASE("Refold - NatFive", "[tree_algorithms::recursion_schemes]") {
    CHECK(refold<int, NatF>(count_algebra, nat_coalgebra, fmap_nat_fn, 5) == 5);
}

TEST_CASE("Refold - EquivalentToFoldOfUnfold", "[tree_algorithms::recursion_schemes]") {
    for (int n = 0; n < 10; ++n) {
        auto via_tree   = fold_fix<int>(count_algebra, fmap_nat_fn, unfold_fix<NatF>(nat_coalgebra, fmap_nat_fn, n));
        auto via_refold = refold<int, NatF>(count_algebra, nat_coalgebra, fmap_nat_fn, n);
        CHECK(via_tree == via_refold);
    }
}

// ---------------------------------------------------------------------
// DEV-01 discriminating tests: non-idempotent, order-sensitive algebras.
// ---------------------------------------------------------------------

TEST_CASE("FoldFix - OrderSensitiveStringAlgebra", "[tree_algorithms::recursion_schemes]") {
    // ((1-2)-3): the string records shape and child order exactly. If
    // fold_fix (or the fmap plumbing) visited children in the wrong order
    // the result would be, e.g., "((2-1)-3)" or "(3-(1-2))".
    auto tree = make_node(make_node(make_leaf(1), make_leaf(2)), make_leaf(3));
    CHECK(fold_fix<std::string>(show_algebra, fmap_tree_fn, tree) == "((1-2)-3)");

    // The mirrored tree must give the mirrored string, not the same one.
    auto mirrored = make_node(make_leaf(3), make_node(make_leaf(2), make_leaf(1)));
    CHECK(fold_fix<std::string>(show_algebra, fmap_tree_fn, mirrored) == "(3-(2-1))");
}

TEST_CASE("FoldFix - NonIdempotentDecrementAlgebra", "[tree_algorithms::recursion_schemes]") {
    // DEV-01's canonical discriminator: "decrement every head by 1" on
    // [10, 10, 10]. Correct answer 27; an implementation that applies the
    // algebra to a layer whose children were folded one extra or one fewer
    // time yields 28 or 26 instead.
    auto decrement_sum = [](const IntListF<int>& layer) -> int {
        return std::visit(overloaded{
                              [](const Nil&) { return 0; },
                              [](const Cons<int>& c) { return (c.head - 1) + *c.tail; },
                          },
                          layer);
    };
    auto list = make_cons(10, make_cons(10, make_cons(10, make_nil())));
    CHECK(fold_fix<int>(decrement_sum, fmap_list_fn, list) == 27);
}

TEST_CASE("UnfoldFix - PositionSensitiveCoalgebra", "[tree_algorithms::recursion_schemes]") {
    // Squares countdown from 4: [16, 9, 4, 1]. The head depends nonlinearly
    // on the seed and the tail on correct seed threading (n - 1), so a wrong
    // recursion depth or wrong child seed changes the serialized spine.
    auto squares_coalgebra = [](int n) -> IntListF<int> {
        if (n <= 0)
            return Nil{};
        return Cons<int>{n * n, make_box<int>(n - 1)};
    };
    auto show_list = [](const IntListF<std::string>& layer) -> std::string {
        return std::visit(overloaded{
                              [](const Nil&) -> std::string { return "end"; },
                              [](const Cons<std::string>& c) { return std::to_string(c.head) + ";" + *c.tail; },
                          },
                          layer);
    };
    auto list = unfold_fix<IntListF>(squares_coalgebra, fmap_list_fn, 4);
    CHECK(fold_fix<std::string>(show_list, fmap_list_fn, list) == "16;9;4;1;end");
}

// ---------------------------------------------------------------------
// Direct verbs over the nonce tree: no Fix anywhere in the pipeline.
// ---------------------------------------------------------------------

TEST_CASE("FoldWith - NonceTreeShape", "[tree_algorithms::recursion_schemes]") {
    //        1
    //       . .
    //      2   3
    //     . .
    //    4   5
    auto tree = nonce_node(1, nonce_node(2, nonce_leaf(4), nonce_leaf(5)), nonce_leaf(3));

    auto shape = fold_with<std::string>(nonce_shape_algebra, fmap_nonce_fn, nonce_project, tree.get());
    CHECK(shape == "(((. 4 .) 2 (. 5 .)) 1 (. 3 .))");

    // Mirrored tree must give the mirrored string (DEV-01 order check).
    auto mirrored       = nonce_node(1, nonce_leaf(3), nonce_node(2, nonce_leaf(5), nonce_leaf(4)));
    auto mirrored_shape = fold_with<std::string>(nonce_shape_algebra, fmap_nonce_fn, nonce_project, mirrored.get());
    CHECK(mirrored_shape == "((. 3 .) 1 ((. 5 .) 2 (. 4 .)))");
}

TEST_CASE("FoldWith - NonceTreeNonIdempotentAlgebra", "[tree_algorithms::recursion_schemes]") {
    // DEV-01 discriminator on the direct path: decrement every value by 1
    // and sum. All values 10, five nodes: correct 45; a traversal that
    // applies the algebra once too often or too rarely at any node misses.
    auto decrement_sum = [](const NonceF<int>& layer) -> int {
        return (layer.value - 1) + layer.left.value_or(0) + layer.right.value_or(0);
    };
    auto tree = nonce_node(10, nonce_node(10, nonce_leaf(10), nonce_leaf(10)), nonce_leaf(10));
    CHECK(fold_with<int>(decrement_sum, fmap_nonce_fn, nonce_project, tree.get()) == 45);
}

TEST_CASE("UnfoldWith - BuildNonceTreeDirectly", "[tree_algorithms::recursion_schemes]") {
    // Balanced build from [lo, hi): value is the midpoint, children are the
    // halves. Built directly into unique_ptr nodes — a move-only carrier no
    // Fix-based pipeline could round-trip by copy.
    auto midpoint_coalgebra = [](const Range& r) -> NonceF<Range> {
        auto [lo, hi] = r;
        int mid       = lo + (hi - lo) / 2;
        if (hi - lo <= 1)
            return {mid, std::optional<Range>{}, std::optional<Range>{}};
        return {mid,
                lo < mid ? std::optional<Range>{Range{lo, mid}} : std::optional<Range>{},
                mid + 1 < hi ? std::optional<Range>{Range{mid + 1, hi}} : std::optional<Range>{}};
    };
    auto nonce_embed = [](NonceF<std::unique_ptr<NonceNode>>&& layer) -> std::unique_ptr<NonceNode> {
        return std::make_unique<NonceNode>(NonceNode{layer.value,
                                                     layer.left ? std::move(*layer.left) : nullptr,
                                                     layer.right ? std::move(*layer.right) : nullptr});
    };

    auto tree = unfold_with<std::unique_ptr<NonceNode>>(midpoint_coalgebra, fmap_nonce_fn, nonce_embed, Range{0, 7});

    // [0,7): mid 3; left [0,3) mid 1 with leaves 0 and 2; right [4,7) mid 5
    // with leaves 4 and 6 — a full BST of seven nodes, in-order 0..6.
    auto shape = fold_with<std::string>(nonce_shape_algebra, fmap_nonce_fn, nonce_project, tree.get());
    CHECK(shape == "(((. 0 .) 1 (. 2 .)) 3 ((. 4 .) 5 (. 6 .)))");
}

TEST_CASE("Refold - FusionMatchesFoldOfUnfold", "[tree_algorithms::recursion_schemes]") {
    // Divide-and-conquer over [0, 5): the parenthesization records the exact
    // intermediate structure, so any divergence between the fused and the
    // materialized pipeline (wrong seed threading, wrong evaluation order)
    // shows up as a different string.
    Range seed{0, 5};
    auto  materialized =
        fold_fix<std::string>(show_algebra, fmap_tree_fn, unfold_fix<TreeF>(split_coalgebra, fmap_tree_fn, seed));
    auto fused = refold<std::string, TreeF>(show_algebra, split_coalgebra, fmap_tree_fn, seed);
    CHECK(fused == materialized);
    // Hand-computed: mid of [0,5) is 2, mid of [2,5) is 3.
    CHECK(fused == "((0-1)-(2-(3-4)))");

    // Non-commutative numeric check on the same structure:
    // (0-1) - (2 - (3-4)) == -1 - 3 == -4.
    CHECK(refold<int, TreeF>(subtract_algebra, split_coalgebra, fmap_tree_fn, seed) == -4);
    CHECK(fold_fix<int>(subtract_algebra, fmap_tree_fn, unfold_fix<TreeF>(split_coalgebra, fmap_tree_fn, seed)) == -4);
}
