// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_TREE_ALGORITHMS_EXPRESSION_HPP
#define BEMAN_TREE_ALGORITHMS_EXPRESSION_HPP

#include <beman/tree_algorithms/config.hpp>

#if BEMAN_TREE_ALGORITHMS_USE_MODULES() && !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

import beman.tree_algorithms;

#else

    #include <beman/tree_algorithms/box.hpp>
    #include <beman/tree_algorithms/child_slot.hpp>
    #include <beman/tree_algorithms/fix.hpp>
    #include <beman/tree_algorithms/fold_map_lookup.hpp>
    #include <beman/tree_algorithms/functor.hpp>
    #include <beman/tree_algorithms/overloaded.hpp>
    #include <beman/tree_algorithms/recursion_schemes_lookup.hpp>

    #if !BEMAN_TREE_ALGORITHMS_USE_MODULES()
        #include <functional>
        #include <type_traits>
        #include <utility>
        #include <variant>
    #endif

namespace beman::tree_algorithms {

// The fixpoint expression tree: the simplest recursive datatype in the
// library, used throughout the paper and tests to demonstrate the pattern.
// Layout is types -> functor_typeclass instance -> smart constructors ->
// algebras: the functor_typeclass partial specialization must be visible
// before any function in this header that dispatches through the lookup
// verbs for ExprF layers.

// ---------------------------------------------------------------------
// The non-recursive base functor.
// ---------------------------------------------------------------------

// 085bb189-a48e-4262-aefd-b64f8755e959
/** Constant leaf alternative; carries an int literal and no recursive
 * positions.
 * @tparam A recursive position placeholder (not yet fixed)
 */
template <typename A>
struct Const {
    int val;
};

/** Addition node alternative; left and right sub-expressions in child
 * slots — boxed at the knot, inline at complete types. */
template <typename A>
struct Add {
    child_slot_t<A> left, right;
};

/** Multiplication node alternative; left and right sub-expressions in
 * child slots. */
template <typename A>
struct Mul {
    child_slot_t<A> left, right;
};

/** Non-recursive expression base functor: one layer of an expression tree
 * with @p A filling the recursive positions. Expr = Fix<ExprF> ties the
 * knot.
 */
template <typename A>
using ExprF = std::variant<Const<A>, Add<A>, Mul<A> >;
// 085bb189-a48e-4262-aefd-b64f8755e959 end

// ---------------------------------------------------------------------
// Functor instance.
// ---------------------------------------------------------------------

// a052ddcb-f05b-41c9-85bf-4c443ab438b7
/** Functor primitive for ExprF<A>: applies @p fn at each recursive
 * position of one layer. Constants have no recursive positions, so the
 * value copies across untouched.
 */
template <typename A>
struct ExprFFunctorImpl {
    template <typename Fn>
    constexpr auto fmap(this auto&&, Fn&& fn, const ExprF<A>& layer) {
        using B = std::remove_cvref_t<std::invoke_result_t<Fn, const A&> >;
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
};

/** Functor map for ExprF<A>: the fmap primitive plus the derived
 * operations from the Functor CRTP base. */
template <typename A>
struct ExprFFunctorMap : Functor<ExprFFunctorImpl<A> > {
    using ExprFFunctorImpl<A>::fmap;
};

/** Registers ExprFFunctorMap as the Functor instance for ExprF<A>. */
template <typename A>
inline constexpr auto functor_typeclass<ExprF<A> > = ExprFFunctorMap<A>{};
// a052ddcb-f05b-41c9-85bf-4c443ab438b7 end

// ---------------------------------------------------------------------
// Smart constructors.
// ---------------------------------------------------------------------

// d0f9b5a2-363f-49ca-9a37-bfb0e6c9ff50
/** The fixed-point expression tree type. */
using Expr = Fix<ExprF>;

/** Build a constant leaf holding @p v. */
constexpr auto const_node(int v) -> Expr { return wrap_fix<ExprF>(ExprF<Expr>{Const<Expr>{v}}); }

/** Build @p l + @p r. */
constexpr auto add_node(Expr l, Expr r) -> Expr {
    return wrap_fix<ExprF>(ExprF<Expr>{Add<Expr>{make_slot<Expr>(std::move(l)), make_slot<Expr>(std::move(r))}});
}

/** Build @p l * @p r. */
constexpr auto mul_node(Expr l, Expr r) -> Expr {
    return wrap_fix<ExprF>(ExprF<Expr>{Mul<Expr>{make_slot<Expr>(std::move(l)), make_slot<Expr>(std::move(r))}});
}
// d0f9b5a2-363f-49ca-9a37-bfb0e6c9ff50 end

// ---------------------------------------------------------------------
// Evaluation.
// ---------------------------------------------------------------------

// bbf73c10-b63e-434a-91f1-d1a3a7c23b96
/** Evaluation algebra: reduces one already-evaluated ExprF layer to an
 * int. Not recursive; fold_fix supplies the recursion.
 */
inline constexpr auto eval_algebra = [](const ExprF<int>& expr) -> int {
    return std::visit(overloaded{
                          [](const Const<int>& c) { return c.val; },
                          [](const Add<int>& a) { return *a.left + *a.right; },
                          [](const Mul<int>& m) { return *m.left * *m.right; },
                      },
                      expr);
};
// bbf73c10-b63e-434a-91f1-d1a3a7c23b96 end

// ---------------------------------------------------------------------
// Elementwise layer fold (for fold_map).
// ---------------------------------------------------------------------

// 8f25579d-443a-4685-b3af-01f8103f6863
/** Folds one ExprF layer elementwise: the elements of an expression tree
 * are the constants, so Const maps its value and Add/Mul just combine
 * their children's results. Note the contrast with eval_algebra: an
 * elementwise fold sees the constants through one combine, where
 * evaluation interprets each alternative — summing the constants of
 * (1 + 2) * 3 gives 6, evaluating it gives 9. ExprF has no empty
 * alternative, so the identity goes unused.
 */
struct ExprLayerFoldMap {
    template <typename MapFn, typename Combine, typename Result>
    constexpr auto operator()(const MapFn&   map_fn,
                              const Combine& combine,
                              const Result& /*identity*/,
                              const ExprF<Result>& layer) const -> Result {
        return std::visit(overloaded{
                              [&](const Const<Result>& c) -> Result { return map_fn(c.val); },
                              [&](const Add<Result>& a) -> Result { return combine(*a.left, *a.right); },
                              [&](const Mul<Result>& m) -> Result { return combine(*m.left, *m.right); },
                          },
                          layer);
    }
};

inline constexpr ExprLayerFoldMap expr_layer_fold_map{};

/** Lookup registration for the ExprF layer fold. */
template <typename A>
inline constexpr auto layer_fold_typeclass<ExprF<A> > = expr_layer_fold_map;
// 8f25579d-443a-4685-b3af-01f8103f6863 end

// 66235297-8e2a-4610-b6a4-a3f2a8837fb0
/** Evaluate an expression tree bottom-up in one fold, resolving fmap
 * through the functor_typeclass instance above. */
constexpr auto eval(const Expr& expr) -> int { return fold_fix<int>(eval_algebra, expr); }
// 66235297-8e2a-4610-b6a4-a3f2a8837fb0 end

} // namespace beman::tree_algorithms

#endif // BEMAN_TREE_ALGORITHMS_USE_MODULES() &&
       // !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_TREE_ALGORITHMS_EXPRESSION_HPP
