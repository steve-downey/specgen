// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/tree_algorithms/expression.hpp>
#include <beman/tree_algorithms/expression.hpp> // Re-inclusion: verifies include guard

#include <beman/tree_algorithms/box.hpp>
#include <beman/tree_algorithms/overloaded.hpp>
#include <beman/tree_algorithms/recursion_schemes.hpp>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

using beman::tree_algorithms::Add;
using beman::tree_algorithms::add_node;
using beman::tree_algorithms::Const;
using beman::tree_algorithms::const_node;
using beman::tree_algorithms::eval;
using beman::tree_algorithms::eval_algebra;
using beman::tree_algorithms::Expr;
using beman::tree_algorithms::ExprF;
using beman::tree_algorithms::fold_fix;
using beman::tree_algorithms::has_functor_instance;
using beman::tree_algorithms::make_slot;
using beman::tree_algorithms::Mul;
using beman::tree_algorithms::mul_node;
using beman::tree_algorithms::overloaded;
using beman::tree_algorithms::refold;
using beman::tree_algorithms::unfold_fix;

namespace {

// The expression tree exercised through all three verbs, each via both the
// lookup API (fmap resolved through functor_typeclass<ExprF<A>>) and the
// explicit-fmap API (fmap passed per call site).

// Hand-rolled explicit fmap for the explicit-fmap primary verbs: written
// independently of the header's functor instance so the two APIs are
// genuinely exercised through separate fmap paths.
template <typename A, typename Fn>
constexpr auto fmap_expr(Fn&& fn, const ExprF<A>& layer) {
    using B = std::remove_cvref_t<std::invoke_result_t<Fn, const A&>>;
    return std::visit(
        overloaded{
            [](const Const<A>& c) -> ExprF<B> { return Const<B>{c.val}; },
            [&fn](const Add<A>& a) -> ExprF<B> {
                return Add<B>{make_slot<B>(std::invoke(fn, *a.left)), make_slot<B>(std::invoke(fn, *a.right))};
            },
            [&fn](const Mul<A>& m) -> ExprF<B> {
                return Mul<B>{make_slot<B>(std::invoke(fn, *m.left)), make_slot<B>(std::invoke(fn, *m.right))};
            },
        },
        layer);
}

inline constexpr auto fmap_expr_fn = [](auto&& fn, const auto& layer) {
    return fmap_expr(std::forward<decltype(fn)>(fn), layer);
};

// Position-marking string algebra: the exact parenthesization records the
// shape and child order of the tree (Decision 7 order sensitivity).
inline auto print_algebra = [](const ExprF<std::string>& expr) -> std::string {
    return std::visit(overloaded{
                          [](const Const<std::string>& c) { return std::to_string(c.val); },
                          [](const Add<std::string>& a) { return "(" + *a.left + " + " + *a.right + ")"; },
                          [](const Mul<std::string>& m) { return "(" + *m.left + " * " + *m.right + ")"; },
                      },
                      expr);
};

// DEV-01 non-idempotent algebra: every constant contributes val - 1. A fold
// that applies the algebra one extra or one fewer time gives a wrong sum.
inline constexpr auto decrement_eval_algebra = [](const ExprF<int>& expr) -> int {
    return std::visit(overloaded{
                          [](const Const<int>& c) { return c.val - 1; },
                          [](const Add<int>& a) { return *a.left + *a.right; },
                          [](const Mul<int>& m) { return *m.left * *m.right; },
                      },
                      expr);
};

// Range-splitting coalgebra for unfold_fix/refold: seed [lo, hi) becomes
// Const{lo} for a single element, otherwise an Add splitting at the
// midpoint. The split point makes the built shape asymmetric, so a print
// fold over the result is order- and structure-sensitive.
using Range = std::pair<int, int>;

inline constexpr auto split_coalgebra = [](const Range& r) -> ExprF<Range> {
    auto [lo, hi] = r;
    if (hi - lo <= 1)
        return Const<Range>{lo};
    int mid = lo + (hi - lo) / 2;
    return Add<Range>{make_slot<Range>(Range{lo, mid}), make_slot<Range>(Range{mid, hi})};
};

// ---------------------------------------------------------------------
// Constexpr coverage (DEV-04): each verb through each API evaluates in a
// constant expression, with non-idempotent algebras.
// ---------------------------------------------------------------------

static_assert(has_functor_instance<ExprF<int>>);
static_assert(has_functor_instance<ExprF<Expr>>);
static_assert(!has_functor_instance<Expr>);

// fold_fix, both APIs: (1 + 2) * 3 == 9, built, folded, and torn down at
// compile time; the decrement variant gives (0 + 1) * 2 == 2, which is
// wrong unless the algebra ran exactly once per node.
constexpr auto eval_via_lookup() -> int {
    auto tree = mul_node(add_node(const_node(1), const_node(2)), const_node(3));
    return eval(tree);
}

static_assert(eval_via_lookup() == 9);

constexpr auto decrement_via_explicit_fmap() -> int {
    auto tree = mul_node(add_node(const_node(1), const_node(2)), const_node(3));
    return fold_fix<int>(decrement_eval_algebra, fmap_expr_fn, tree);
}

static_assert(decrement_via_explicit_fmap() == 2);

// unfold_fix, both APIs: [1, 4) splits into Add(Const 1, Add(Const 2,
// Const 3)); evaluated as 1 + (2 + 3) == 6, and decrement-evaluated as
// 0 + (1 + 2) == 3.
constexpr auto unfold_then_eval_via_lookup() -> int {
    auto tree = unfold_fix<ExprF>(split_coalgebra, Range{1, 4});
    return eval(tree);
}

static_assert(unfold_then_eval_via_lookup() == 6);

constexpr auto unfold_then_eval_via_explicit_fmap() -> int {
    auto tree = unfold_fix<ExprF>(split_coalgebra, fmap_expr_fn, Range{1, 4});
    return fold_fix<int>(decrement_eval_algebra, fmap_expr_fn, tree);
}

static_assert(unfold_then_eval_via_explicit_fmap() == 3);

// refold, both APIs: fused pipelines agree with the two-pass results above.
static_assert(refold<int, ExprF>(eval_algebra, split_coalgebra, Range{1, 4}) == 6);
static_assert(refold<int, ExprF>(decrement_eval_algebra, split_coalgebra, fmap_expr_fn, Range{1, 4}) == 3);

} // namespace

TEST_CASE("Expression - EvalViaLookupFold", "[tree_algorithms::expression]") {
    // 5033568c-a02f-44b0-964b-7076ebb06d43
    auto tree = mul_node(add_node(const_node(1), const_node(2)), const_node(3));
    // 5033568c-a02f-44b0-964b-7076ebb06d43 end
    CHECK(eval(tree) == 9);
    CHECK(fold_fix<int>(eval_algebra, tree) == 9);
}

TEST_CASE("Expression - EvalViaExplicitFmapFold", "[tree_algorithms::expression]") {
    auto tree = mul_node(add_node(const_node(1), const_node(2)), const_node(3));
    CHECK(fold_fix<int>(eval_algebra, fmap_expr_fn, tree) == 9);
}

TEST_CASE("Expression - PrintAlgebraRecordsShapeAndOrder", "[tree_algorithms::expression]") {
    // c1858641-8184-4907-ad06-14744a4ac1d2
    auto e      = mul_node(add_node(const_node(1), const_node(2)), const_node(4));
    auto result = fold_fix<std::string>(print_algebra, e);
    CHECK(result == "((1 + 2) * 4)");
    // c1858641-8184-4907-ad06-14744a4ac1d2 end

    // The mirrored tree must give the mirrored string: a fold whose fmap
    // visited children right-to-left would swap these.
    auto mirrored = mul_node(const_node(4), add_node(const_node(2), const_node(1)));
    CHECK(fold_fix<std::string>(print_algebra, mirrored) == "(4 * (2 + 1))");

    // The explicit-fmap API prints identically.
    CHECK(fold_fix<std::string>(print_algebra, fmap_expr_fn, e) == "((1 + 2) * 4)");
}

TEST_CASE("Expression - NonIdempotentDecrementAlgebra", "[tree_algorithms::expression]") {
    // DEV-01 discriminator: 10 + (10 + 10) of decremented constants is
    // 9 + 9 + 9 == 27; applying the algebra an extra or a missing time
    // yields 26 or 28 instead.
    auto tree = add_node(const_node(10), add_node(const_node(10), const_node(10)));
    CHECK(fold_fix<int>(decrement_eval_algebra, tree) == 27);
    CHECK(fold_fix<int>(decrement_eval_algebra, fmap_expr_fn, tree) == 27);
}

TEST_CASE("Expression - UnfoldBuildsTheExpectedShape", "[tree_algorithms::expression]") {
    // Split [0, 5): mid of [0,5) is 2, mid of [2,5) is 3. The
    // parenthesization pins the exact shape either API must build.
    Range seed{0, 5};
    auto  via_lookup   = unfold_fix<ExprF>(split_coalgebra, seed);
    auto  via_explicit = unfold_fix<ExprF>(split_coalgebra, fmap_expr_fn, seed);
    CHECK(fold_fix<std::string>(print_algebra, via_lookup) == "((0 + 1) + (2 + (3 + 4)))");
    CHECK(fold_fix<std::string>(print_algebra, via_explicit) == "((0 + 1) + (2 + (3 + 4)))");
}

TEST_CASE("Expression - RefoldMatchesFoldOfUnfold", "[tree_algorithms::expression]") {
    Range seed{0, 5};
    auto  materialized = fold_fix<std::string>(print_algebra, unfold_fix<ExprF>(split_coalgebra, seed));

    auto fused_lookup   = refold<std::string, ExprF>(print_algebra, split_coalgebra, seed);
    auto fused_explicit = refold<std::string, ExprF>(print_algebra, split_coalgebra, fmap_expr_fn, seed);
    CHECK(fused_lookup == materialized);
    CHECK(fused_explicit == materialized);

    // Non-idempotent numeric check on the same structure: five constants
    // 0..4 decremented sum to (0+1+2+3+4) - 5 == 5.
    CHECK(refold<int, ExprF>(decrement_eval_algebra, split_coalgebra, seed) == 5);
}
