// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_TREE_ALGORITHMS_RECURSION_SCHEMES_LOOKUP_HPP
#define BEMAN_TREE_ALGORITHMS_RECURSION_SCHEMES_LOOKUP_HPP

#include <beman/tree_algorithms/config.hpp>

#if BEMAN_TREE_ALGORITHMS_USE_MODULES() && !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

import beman.tree_algorithms;

#else

    #include <beman/tree_algorithms/functor.hpp>
    #include <beman/tree_algorithms/recursion_schemes.hpp>

    #if !BEMAN_TREE_ALGORITHMS_USE_MODULES()
        #include <type_traits>
        #include <utility>
    #endif

namespace beman::tree_algorithms {

// Lookup-based overloads of the three recursion verbs (Decision 4).
//
// These are the everyday spelling: fmap is resolved through
// functor_typeclass keyed on the concrete layer type, so call sites pass
// only the algebra/coalgebra and the data. The explicit-fmap primary verbs
// in recursion_schemes.hpp remain the per-call-site escape hatch — and the
// surface that stays fully valid independent of the Paper A lookup
// mechanism this header consumes.

/** Look up the functor_typeclass instance for the concrete layer type of
 * @p layer and apply its fmap.
 *
 * This is the single point where the verb bodies bridge into
 * functor_typeclass dispatch: instead of threading an explicit fmap_fn
 * through every recursive call, they call layer_fmap(fn, layer) and the
 * lookup keyed on std::remove_cvref_t<Layer> finds the right fmap.
 */
// f6936d93-a4aa-4e85-afd1-092d4bbb1ba1
template <class Fn, class Layer>
constexpr auto layer_fmap(Fn&& fn, const Layer& layer) {
    static_assert(has_functor_instance<std::remove_cvref_t<Layer> >,
                  "no Functor instance: specialize beman::tree_algorithms::functor_typeclass "
                  "for this layer type, or use the explicit-fmap overloads in "
                  "recursion_schemes.hpp");
    return functor_typeclass<std::remove_cvref_t<Layer> >.fmap(std::forward<Fn>(fn), layer);
}
// f6936d93-a4aa-4e85-afd1-092d4bbb1ba1 end

/** Recursive fold (reduce), resolving fmap through functor_typeclass.
 * Equivalent to fold_fix<Result>(algebra, fmap_fn, tree) with the looked-up
 * fmap for F<...>.
 * @tparam Result the fold carrier; must be given explicitly
 */
// d4f69d2f-787b-4741-ae82-971f0ff36b9e
template <typename Result, template <typename> class F, typename Algebra>
constexpr auto fold_fix(const Algebra& algebra, const Fix<F>& tree) -> Result {
    const auto& layer = unwrap_fix(tree);
    auto        evaluated =
        layer_fmap([&](const Fix<F>& child) -> Result { return fold_fix<Result>(algebra, child); }, layer);
    return algebra(evaluated);
}
// d4f69d2f-787b-4741-ae82-971f0ff36b9e end

/** Recursive build (unfold), resolving fmap through functor_typeclass.
 * Equivalent to unfold_fix<F>(coalgebra, fmap_fn, seed) with the looked-up
 * fmap for F<...>.
 * @tparam F the base functor; must be given explicitly
 */
// 2df7f7e0-ee8b-42b6-a0a8-1dfed05c8230
template <template <typename> class F, typename Coalgebra, typename Seed>
constexpr auto unfold_fix(const Coalgebra& coalgebra, const Seed& seed) -> Fix<F> {
    auto layer    = coalgebra(seed);
    auto expanded = layer_fmap([&](const Seed& child) -> Fix<F> { return unfold_fix<F>(coalgebra, child); }, layer);
    return wrap_fix<F>(std::move(expanded));
}
// 2df7f7e0-ee8b-42b6-a0a8-1dfed05c8230 end

/** Fused unfold-then-fold, resolving fmap through functor_typeclass.
 * Equivalent to refold<Result, F>(algebra, coalgebra, fmap_fn, seed) with
 * the looked-up fmap for F<...>.
 * @tparam Result the fold carrier; must be given explicitly
 * @tparam F      the base functor; must be given explicitly
 */
// c75161e3-1aad-4549-86cc-1dc40d6623c8
template <typename Result, template <typename> class F, typename Algebra, typename Coalgebra, typename Seed>
constexpr auto refold(const Algebra& algebra, const Coalgebra& coalgebra, const Seed& seed) -> Result {
    auto layer = coalgebra(seed);
    auto evaluated =
        layer_fmap([&](const Seed& child) -> Result { return refold<Result, F>(algebra, coalgebra, child); }, layer);
    return algebra(evaluated);
}
// c75161e3-1aad-4549-86cc-1dc40d6623c8 end

} // namespace beman::tree_algorithms

#endif // BEMAN_TREE_ALGORITHMS_USE_MODULES() &&
       // !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_TREE_ALGORITHMS_RECURSION_SCHEMES_LOOKUP_HPP
