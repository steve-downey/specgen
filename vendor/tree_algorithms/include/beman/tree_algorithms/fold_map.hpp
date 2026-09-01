// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_TREE_ALGORITHMS_FOLD_MAP_HPP
#define BEMAN_TREE_ALGORITHMS_FOLD_MAP_HPP

#include <beman/tree_algorithms/config.hpp>

#if BEMAN_TREE_ALGORITHMS_USE_MODULES() && !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

import beman.tree_algorithms;

#else

    #include <beman/tree_algorithms/fix.hpp>
    #include <beman/tree_algorithms/recursion_schemes.hpp>

namespace beman::tree_algorithms {

// Elementwise fold over trees: map each element to the result type, then
// combine with an associative operation and its identity. Where the
// structural verbs consume a whole layer at a time, fold_map sees only
// elements — length, sums, to-string renderings, any/all queries are all
// fold_maps.
//
// The per-representation ingredient is layer_fold: the callable that
// folds ONE layer, combining that layer's mapped elements with the
// already-folded child results in the representation's contractual
// traversal order (in-order for binary trees). Everything here is an
// explicit callable — like the explicit-fmap recursion verbs, this tier
// has no customization-mechanism dependency at all.
//
// The derivation is the implementation: fold_map over Fix<F> IS fold_fix
// with a monoid algebra, and fold_map over a projected tree IS fold_with
// with the same algebra. Elementwise folding is derived from the
// structural verbs, not parallel machinery.

/** Elementwise fold over a Fix<F> tree.
 *
 * @tparam Result the fold carrier; must be given explicitly
 * @param map_fn     callable Element -> Result
 * @param combine    callable (Result, Result) -> Result, associative
 * @param identity   the identity of @p combine (empty/absent positions)
 * @param layer_fold callable (map_fn, combine, identity, F<Result>) ->
 *                   Result — folds one layer, children already folded
 * @param fmap_fn    callable (Fn, const F<A>&) -> F<B> — the layer's fmap
 * @param tree       the fixed-point value to fold
 */
// 52588460-d05a-43f9-97cc-01aa06732730
template <typename Result,
          template <typename> class F,
          typename MapFn,
          typename Combine,
          typename LayerFold,
          typename FMap>
constexpr auto fold_map(const MapFn&     map_fn,
                        const Combine&   combine,
                        const Result&    identity,
                        const LayerFold& layer_fold,
                        const FMap&      fmap_fn,
                        const Fix<F>&    tree) -> Result {
    auto algebra = [&](const auto& layer) -> Result { return layer_fold(map_fn, combine, identity, layer); };
    return fold_fix<Result>(algebra, fmap_fn, tree);
}
// 52588460-d05a-43f9-97cc-01aa06732730 end

/** Elementwise fold over a tree in its own representation.
 *
 * As above, plus @p project exposing one layer of the user's tree at a
 * time (the same projection fold_with takes); no Fix is materialized.
 */
// 5bf78776-81e7-4b66-8487-6615adc33b17
template <typename Result,
          typename MapFn,
          typename Combine,
          typename LayerFold,
          typename FMap,
          typename Project,
          typename Tree>
constexpr auto fold_map(const MapFn&     map_fn,
                        const Combine&   combine,
                        const Result&    identity,
                        const LayerFold& layer_fold,
                        const FMap&      fmap_fn,
                        const Project&   project,
                        const Tree&      tree) -> Result {
    auto algebra = [&](const auto& layer) -> Result { return layer_fold(map_fn, combine, identity, layer); };
    return fold_with<Result>(algebra, fmap_fn, project, tree);
}
// 5bf78776-81e7-4b66-8487-6615adc33b17 end

} // namespace beman::tree_algorithms

#endif // BEMAN_TREE_ALGORITHMS_USE_MODULES() &&
       // !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_TREE_ALGORITHMS_FOLD_MAP_HPP
