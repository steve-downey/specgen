// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/tree_algorithms/recursion_schemes_lookup.hpp>
#include <beman/tree_algorithms/recursion_schemes_lookup.hpp> // Re-inclusion: verifies include guard

#include <beman/tree_algorithms/box.hpp>
#include <beman/tree_algorithms/overloaded.hpp>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

using beman::tree_algorithms::Box;
using beman::tree_algorithms::Fix;
using beman::tree_algorithms::fold_fix;
using beman::tree_algorithms::Functor;
using beman::tree_algorithms::has_functor_instance;
using beman::tree_algorithms::make_box;
using beman::tree_algorithms::overloaded;
using beman::tree_algorithms::refold;
using beman::tree_algorithms::unfold_fix;
using beman::tree_algorithms::wrap_fix;

// Representative subset of the recursion_schemes suite, driven through the
// two-argument lookup overloads: fmap is resolved via functor_typeclass
// keyed on the concrete layer type instead of being passed per call site.
// Each functor family is laid out types -> functor_typeclass instance ->
// smart constructors/algebras, so the partial specialization is visible
// before any function that triggers lookup dispatch for that layer.

// ---------------------------------------------------------------------
// NatF — unary naturals.
// ---------------------------------------------------------------------

namespace lookup_test {

struct Zero {};

template <typename A>
struct Succ {
    Box<A> pred;
};

template <typename A>
using NatF = std::variant<Zero, Succ<A>>;

using Nat = Fix<NatF>;

template <typename A>
struct NatFFunctorImpl {
    template <typename Fn>
    constexpr auto fmap(this auto&&, Fn&& fn, const NatF<A>& layer) {
        using B = std::remove_cvref_t<std::invoke_result_t<Fn, const A&>>;
        return std::visit(
            overloaded{
                [](const Zero&) -> NatF<B> { return Zero{}; },
                [&fn](const Succ<A>& s) -> NatF<B> { return Succ<B>{make_box<B>(std::invoke(fn, *s.pred))}; },
            },
            layer);
    }
};

template <typename A>
struct NatFFunctorMap : Functor<NatFFunctorImpl<A>> {
    using NatFFunctorImpl<A>::fmap;
};

} // namespace lookup_test

namespace beman::tree_algorithms {

template <typename A>
inline constexpr auto functor_typeclass<lookup_test::NatF<A>> = lookup_test::NatFFunctorMap<A>{};

} // namespace beman::tree_algorithms

namespace lookup_test {

constexpr auto make_zero() -> Nat { return wrap_fix<NatF>(NatF<Nat>{Zero{}}); }

constexpr auto make_succ(Nat n) -> Nat { return wrap_fix<NatF>(NatF<Nat>{Succ<Nat>{make_box<Nat>(std::move(n))}}); }

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

} // namespace lookup_test

// ---------------------------------------------------------------------
// IntListF — cons-lists of int, for the DEV-01 non-idempotent algebras.
// ---------------------------------------------------------------------

namespace lookup_test {

struct Nil {};

template <typename A>
struct Cons {
    int    head;
    Box<A> tail;
};

template <typename A>
using IntListF = std::variant<Nil, Cons<A>>;

using IntList = Fix<IntListF>;

template <typename A>
struct IntListFFunctorImpl {
    template <typename Fn>
    constexpr auto fmap(this auto&&, Fn&& fn, const IntListF<A>& layer) {
        using B = std::remove_cvref_t<std::invoke_result_t<Fn, const A&>>;
        return std::visit(overloaded{
                              [](const Nil&) -> IntListF<B> { return Nil{}; },
                              [&fn](const Cons<A>& c) -> IntListF<B> {
                                  return Cons<B>{c.head, make_box<B>(std::invoke(fn, *c.tail))};
                              },
                          },
                          layer);
    }
};

template <typename A>
struct IntListFFunctorMap : Functor<IntListFFunctorImpl<A>> {
    using IntListFFunctorImpl<A>::fmap;
};

} // namespace lookup_test

namespace beman::tree_algorithms {

template <typename A>
inline constexpr auto functor_typeclass<lookup_test::IntListF<A>> = lookup_test::IntListFFunctorMap<A>{};

} // namespace beman::tree_algorithms

namespace lookup_test {

constexpr auto make_nil() -> IntList { return wrap_fix<IntListF>(IntListF<IntList>{Nil{}}); }

constexpr auto make_cons(int head, IntList tail) -> IntList {
    return wrap_fix<IntListF>(IntListF<IntList>{Cons<IntList>{head, make_box<IntList>(std::move(tail))}});
}

} // namespace lookup_test

// ---------------------------------------------------------------------
// TreeF — external binary tree, payload at leaves, for order-sensitive
// (non-commutative) fold and structure-sensitive refold tests.
// ---------------------------------------------------------------------

namespace lookup_test {

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

template <typename A>
struct TreeFFunctorImpl {
    template <typename Fn>
    constexpr auto fmap(this auto&&, Fn&& fn, const TreeF<A>& layer) {
        using B = std::remove_cvref_t<std::invoke_result_t<Fn, const A&>>;
        return std::visit(overloaded{
                              [](const Leaf& l) -> TreeF<B> { return Leaf{l.value}; },
                              [&fn](const Node<A>& n) -> TreeF<B> {
                                  return Node<B>{make_box<B>(std::invoke(fn, *n.left)),
                                                 make_box<B>(std::invoke(fn, *n.right))};
                              },
                          },
                          layer);
    }
};

template <typename A>
struct TreeFFunctorMap : Functor<TreeFFunctorImpl<A>> {
    using TreeFFunctorImpl<A>::fmap;
};

} // namespace lookup_test

namespace beman::tree_algorithms {

template <typename A>
inline constexpr auto functor_typeclass<lookup_test::TreeF<A>> = lookup_test::TreeFFunctorMap<A>{};

} // namespace beman::tree_algorithms

namespace lookup_test {

constexpr auto make_leaf(int v) -> IntTree { return wrap_fix<TreeF>(TreeF<IntTree>{Leaf{v}}); }

constexpr auto make_node(IntTree l, IntTree r) -> IntTree {
    return wrap_fix<TreeF>(
        TreeF<IntTree>{Node<IntTree>{make_box<IntTree>(std::move(l)), make_box<IntTree>(std::move(r))}});
}

// Non-commutative numeric algebra: leaf -> value, node -> left - right.
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

// Range-splitting coalgebra: seed [lo, hi) becomes Leaf{lo} for a single
// element, otherwise a Node splitting at the midpoint.
using Range = std::pair<int, int>;

inline constexpr auto split_coalgebra = [](const Range& r) -> TreeF<Range> {
    auto [lo, hi] = r;
    if (hi - lo <= 1)
        return Leaf{lo};
    int mid = lo + (hi - lo) / 2;
    return Node<Range>{make_box<Range>(Range{lo, mid}), make_box<Range>(Range{mid, hi})};
};

} // namespace lookup_test

using namespace lookup_test;

namespace {

// Every layer family above is found through the lookup variable.
static_assert(has_functor_instance<NatF<int>>);
static_assert(has_functor_instance<IntListF<int>>);
static_assert(has_functor_instance<TreeF<int>>);
static_assert(!has_functor_instance<int>);

// ---------------------------------------------------------------------
// Constexpr coverage (DEV-04): at least one static_assert per lookup verb,
// each using an order-sensitive computation.
// ---------------------------------------------------------------------

// fold_fix lookup form: ((1 - 2) - 3) == -4; any child swap or
// re-association differs.
constexpr auto fold_subtracts_in_order() -> int {
    auto tree = make_node(make_node(make_leaf(1), make_leaf(2)), make_leaf(3));
    return fold_fix<int>(subtract_algebra, tree);
}

static_assert(fold_subtracts_in_order() == -4);

// unfold_fix lookup form: countdown-of-squares from 3 is [9, 4, 1]; folded
// with the non-commutative combine h + 2 * rest: 9 + 2*(4 + 2*(1 + 2*0)) == 21.
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
    auto list = unfold_fix<IntListF>(squares_coalgebra, 3);
    return fold_fix<int>(weigh_algebra, list);
}

static_assert(unfold_squares_then_weigh() == 21);

// refold lookup form: split [1, 4) -> Node(Leaf 1, Node(Leaf 2, Leaf 3));
// subtraction gives 1 - (2 - 3) == 2.
constexpr auto refold_split_subtract() -> int {
    return refold<int, TreeF>(subtract_algebra, split_coalgebra, Range{1, 4});
}

static_assert(refold_split_subtract() == 2);

} // namespace

// ---------------------------------------------------------------------
// Ported fixpoint suite (NatF) through the lookup overloads.
// ---------------------------------------------------------------------

TEST_CASE("FoldFix lookup - NatZero", "[tree_algorithms::recursion_schemes_lookup]") {
    auto zero = make_zero();
    CHECK(fold_fix<int>(count_algebra, zero) == 0);
}

TEST_CASE("FoldFix lookup - NatFive", "[tree_algorithms::recursion_schemes_lookup]") {
    auto n = make_zero();
    for (int i = 0; i < 5; ++i) {
        n = make_succ(std::move(n));
    }
    CHECK(fold_fix<int>(count_algebra, n) == 5);
}

TEST_CASE("UnfoldFix lookup - NatRoundTrip", "[tree_algorithms::recursion_schemes_lookup]") {
    CHECK(fold_fix<int>(count_algebra, unfold_fix<NatF>(nat_coalgebra, 0)) == 0);
    CHECK(fold_fix<int>(count_algebra, unfold_fix<NatF>(nat_coalgebra, 5)) == 5);
}

TEST_CASE("Refold lookup - EquivalentToFoldOfUnfold", "[tree_algorithms::recursion_schemes_lookup]") {
    for (int n = 0; n < 10; ++n) {
        auto via_tree   = fold_fix<int>(count_algebra, unfold_fix<NatF>(nat_coalgebra, n));
        auto via_refold = refold<int, NatF>(count_algebra, nat_coalgebra, n);
        CHECK(via_tree == via_refold);
        CHECK(via_refold == n);
    }
}

// ---------------------------------------------------------------------
// DEV-01 discriminating tests: non-idempotent, order-sensitive algebras.
// ---------------------------------------------------------------------

TEST_CASE("FoldFix lookup - OrderSensitiveStringAlgebra", "[tree_algorithms::recursion_schemes_lookup]") {
    // ((1-2)-3): the string records shape and child order exactly. If the
    // looked-up fmap visited children in the wrong order the result would
    // be, e.g., "((2-1)-3)" or "(3-(1-2))".
    auto tree = make_node(make_node(make_leaf(1), make_leaf(2)), make_leaf(3));
    CHECK(fold_fix<std::string>(show_algebra, tree) == "((1-2)-3)");

    // The mirrored tree must give the mirrored string, not the same one.
    auto mirrored = make_node(make_leaf(3), make_node(make_leaf(2), make_leaf(1)));
    CHECK(fold_fix<std::string>(show_algebra, mirrored) == "(3-(2-1))");
}

TEST_CASE("FoldFix lookup - NonIdempotentDecrementAlgebra", "[tree_algorithms::recursion_schemes_lookup]") {
    // DEV-01's canonical discriminator: "decrement every head by 1" on
    // [10, 10, 10]. Correct answer 27; a fold that applies the algebra one
    // extra or one fewer time yields 28 or 26 instead.
    auto decrement_sum = [](const IntListF<int>& layer) -> int {
        return std::visit(overloaded{
                              [](const Nil&) { return 0; },
                              [](const Cons<int>& c) { return (c.head - 1) + *c.tail; },
                          },
                          layer);
    };
    auto list = make_cons(10, make_cons(10, make_cons(10, make_nil())));
    CHECK(fold_fix<int>(decrement_sum, list) == 27);
}

TEST_CASE("Refold lookup - FusionMatchesFoldOfUnfold", "[tree_algorithms::recursion_schemes_lookup]") {
    // Divide-and-conquer over [0, 5): the parenthesization records the exact
    // intermediate structure, so any divergence between the fused and the
    // materialized pipeline shows up as a different string.
    Range seed{0, 5};
    auto  materialized = fold_fix<std::string>(show_algebra, unfold_fix<TreeF>(split_coalgebra, seed));
    auto  fused        = refold<std::string, TreeF>(show_algebra, split_coalgebra, seed);
    CHECK(fused == materialized);
    // Hand-computed: mid of [0,5) is 2, mid of [2,5) is 3.
    CHECK(fused == "((0-1)-(2-(3-4)))");

    // Non-commutative numeric check on the same structure:
    // (0-1) - (2 - (3-4)) == -1 - 3 == -4.
    CHECK(refold<int, TreeF>(subtract_algebra, split_coalgebra, seed) == -4);
}

TEST_CASE("Lookup overloads match the explicit-fmap escape hatch", "[tree_algorithms::recursion_schemes_lookup]") {
    // The two-argument everyday form and the three-argument per-call-site
    // form must agree when the explicit fmap is the looked-up one.
    auto explicit_fmap = [](auto&& fn, const auto& layer) {
        return beman::tree_algorithms::layer_fmap(std::forward<decltype(fn)>(fn), layer);
    };

    auto tree = make_node(make_node(make_leaf(1), make_leaf(2)), make_node(make_leaf(3), make_leaf(4)));
    CHECK(fold_fix<int>(subtract_algebra, tree) == fold_fix<int>(subtract_algebra, explicit_fmap, tree));

    Range seed{0, 5};
    CHECK(refold<int, TreeF>(subtract_algebra, split_coalgebra, seed) ==
          refold<int, TreeF>(subtract_algebra, split_coalgebra, explicit_fmap, seed));
}
