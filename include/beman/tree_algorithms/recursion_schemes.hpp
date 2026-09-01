// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_TREE_ALGORITHMS_RECURSION_SCHEMES_HPP
#define BEMAN_TREE_ALGORITHMS_RECURSION_SCHEMES_HPP

#include <beman/tree_algorithms/config.hpp>

#if BEMAN_TREE_ALGORITHMS_USE_MODULES() && !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

import beman.tree_algorithms;

#else

    #include <beman/tree_algorithms/fix.hpp>

    #if !BEMAN_TREE_ALGORITHMS_USE_MODULES()
        #include <utility>
    #endif

namespace beman::tree_algorithms {

// The three recursion verbs over Fix<F>. Each takes the base functor's fmap
// explicitly as a callable parameter; result carriers are leading explicit
// template parameters because C++ cannot infer fold carriers through
// recursive calls.
//
// Lookup-based overloads (which resolve fmap through functor_typeclass
// lookup) live in recursion_schemes_lookup.hpp (Decision 4); this header has no
// typeclass dependency.

/** Recursive fold (reduce): collapse a Fix<F> tree bottom-up into a Result.
 *
 * Unwraps one layer, recursively folds every child position via @p fmap_fn,
 * then combines the fully evaluated layer with @p algebra.
 * @tparam Result the fold carrier; must be given explicitly
 * @param algebra  callable F<Result> -> Result
 * @param fmap_fn  callable (Fn, const F<A>&) -> F<B> — the functor's fmap
 * @param tree     the fixed-point value to fold
 */
// 6859fdfc-3e1c-4d8a-9375-521df52ff499
template <typename Result, template <typename> class F, typename Algebra, typename FMap>
constexpr auto fold_fix(const Algebra& algebra, const FMap& fmap_fn, const Fix<F>& tree) -> Result {
    const auto& layer = unwrap_fix(tree);
    auto        evaluated =
        fmap_fn([&](const Fix<F>& child) -> Result { return fold_fix<Result>(algebra, fmap_fn, child); }, layer);
    return algebra(evaluated);
}
// 6859fdfc-3e1c-4d8a-9375-521df52ff499 end

/** Recursive build (unfold): grow a Fix<F> tree top-down from a seed.
 *
 * Expands @p seed one layer with @p coalgebra, recursively unfolds every
 * child seed via @p fmap_fn, then wraps the completed layer.
 * @tparam F the base functor; must be given explicitly
 * @param coalgebra callable Seed -> F<Seed>
 * @param fmap_fn   callable (Fn, const F<A>&) -> F<B> — the functor's fmap
 * @param seed      the starting seed
 */
template <template <typename> class F, typename Coalgebra, typename FMap, typename Seed>
constexpr auto unfold_fix(const Coalgebra& coalgebra, const FMap& fmap_fn, const Seed& seed) -> Fix<F> {
    auto layer = coalgebra(seed);
    auto expanded =
        fmap_fn([&](const Seed& child) -> Fix<F> { return unfold_fix<F>(coalgebra, fmap_fn, child); }, layer);
    return wrap_fix<F>(std::move(expanded));
}

/** Fused unfold-then-fold: compute fold_fix(algebra, unfold_fix(coalgebra, seed))
 * without materializing the intermediate Fix<F> tree.
 *
 * Each layer produced by @p coalgebra is consumed by @p algebra as soon as
 * its children have been recursively refolded.
 * @tparam Result the fold carrier; must be given explicitly
 * @tparam F      the base functor; must be given explicitly
 */
template <typename Result,
          template <typename> class F,
          typename Algebra,
          typename Coalgebra,
          typename FMap,
          typename Seed>
constexpr auto refold(const Algebra& algebra, const Coalgebra& coalgebra, const FMap& fmap_fn, const Seed& seed)
    -> Result {
    auto layer     = coalgebra(seed);
    auto evaluated = fmap_fn(
        [&](const Seed& child) -> Result { return refold<Result, F>(algebra, coalgebra, fmap_fn, child); }, layer);
    return algebra(evaluated);
}

// The direct verbs: fold and build over a tree in its own representation,
// with no Fix<F> and no conversion. The extra ingredient is supplied
// explicitly — a projection exposing one layer of the user's tree as a
// base-functor value, or its dual, an embedding rebuilding one layer of
// the user's tree from a completed base-functor layer.
//
// fold_with(algebra, fmap_fn, project, tree) is refold with project as
// the coalgebra; unfold_with(coalgebra, fmap_fn, embed, seed) is refold
// with embed as the algebra. fold_fix is the degenerate case where
// project is unwrap_fix. The child handles inside a projected layer need
// not be the tree type itself — a projection may hand out pointers or
// other cheap handles, and the recursion follows whatever project accepts.

/** Recursive fold over a tree in its own representation.
 *
 * Projects one layer of @p tree with @p project, recursively folds every
 * child handle in that layer via @p fmap_fn, then combines the fully
 * evaluated layer with @p algebra. Equivalent to
 * refold(algebra, project, fmap_fn, tree); no Fix is materialized.
 * @tparam Result the fold carrier; must be given explicitly
 * @param algebra  callable F<Result> -> Result
 * @param fmap_fn  callable (Fn, const F<A>&) -> F<B> — the layer's fmap
 * @param project  callable Tree -> F<Handle>, one layer, children as
 *                 handles that @p project itself accepts
 * @param tree     the tree (or child handle) to fold
 */
// 6c596985-e990-4808-a504-2b651c34cebc
template <typename Result, typename Algebra, typename FMap, typename Project, typename Tree>
constexpr auto fold_with(const Algebra& algebra, const FMap& fmap_fn, const Project& project, const Tree& tree)
    -> Result {
    auto layer     = project(tree);
    auto evaluated = fmap_fn(
        [&](const auto& child) -> Result { return fold_with<Result>(algebra, fmap_fn, project, child); }, layer);
    return algebra(evaluated);
}
// 6c596985-e990-4808-a504-2b651c34cebc end

/** Recursive build into a tree's own representation.
 *
 * Expands @p seed one layer with @p coalgebra, recursively unfolds every
 * child seed via @p fmap_fn, then rebuilds one layer of the target
 * representation with @p embed. Equivalent to
 * refold(embed, coalgebra, fmap_fn, seed); no Fix is materialized.
 * @tparam Tree the build carrier (the user's tree type); must be given
 *              explicitly
 * @param coalgebra callable Seed -> F<Seed>
 * @param fmap_fn   callable (Fn, const F<A>&) -> F<B> — the layer's fmap
 * @param embed     callable F<Tree> -> Tree, rebuilding one layer
 * @param seed      the starting seed
 */
// d160af7b-9a3c-4593-b8f5-fc1ff0404b37
template <typename Tree, typename Coalgebra, typename FMap, typename Embed, typename Seed>
constexpr auto unfold_with(const Coalgebra& coalgebra, const FMap& fmap_fn, const Embed& embed, const Seed& seed)
    -> Tree {
    auto layer = coalgebra(seed);
    auto expanded =
        fmap_fn([&](const auto& child) -> Tree { return unfold_with<Tree>(coalgebra, fmap_fn, embed, child); }, layer);
    return embed(std::move(expanded));
}
// d160af7b-9a3c-4593-b8f5-fc1ff0404b37 end

} // namespace beman::tree_algorithms

#endif // BEMAN_TREE_ALGORITHMS_USE_MODULES() &&
       // !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_TREE_ALGORITHMS_RECURSION_SCHEMES_HPP
