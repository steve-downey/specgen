// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_TREE_ALGORITHMS_FIX_HPP
#define BEMAN_TREE_ALGORITHMS_FIX_HPP

#include <beman/tree_algorithms/config.hpp>

#if BEMAN_TREE_ALGORITHMS_USE_MODULES() && !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

import beman.tree_algorithms;

#else

    #if !BEMAN_TREE_ALGORITHMS_USE_MODULES()
        #include <utility>
    #endif

namespace beman::tree_algorithms {

/** Fixed-point combinator that ties the recursive knot for a functor @p F.
 *
 * Fix<F> is the iso-recursive type satisfying Fix<F> ≅ F<Fix<F>>.
 * The single data member @c inner holds one unwrapped layer; wrap/unwrap
 * are the iso-recursive isomorphism boundary.
 * F's recursive positions need indirection only here, where Fix<F> is
 * incomplete — hold them in child_slot_t (child_slot.hpp), which is a Box
 * exactly at the knot and inline storage at every complete type.
 * @tparam F unary template functor (takes the recursive position as its param)
 */
// 57d4bd6e-c8c7-4806-afd3-2e42aec8ae27
template <template <typename> class F>
struct Fix {
    F<Fix<F> > inner;
};
// 57d4bd6e-c8c7-4806-afd3-2e42aec8ae27 end

// 53775b7e-8a78-4b79-885b-046f6232d7a3
/** Wrap one layer of @p F into the fixed-point type, consuming the
 * layer: one move, whether the argument is a prvalue or an xvalue. */
template <template <typename> class F>
constexpr auto wrap_fix(F<Fix<F> >&& layer) -> Fix<F> {
    return Fix<F>{std::move(layer)};
}

/** Wrap a layer the caller keeps: one copy into the fixed point. */
template <template <typename> class F>
constexpr auto wrap_fix(const F<Fix<F> >& layer) -> Fix<F> {
    return Fix<F>{layer};
}

/** Unwrap one layer from a fixed-point value, exposing F<Fix<F>>.
 * Reads in place: no copy, no move. */
template <template <typename> class F>
constexpr auto unwrap_fix(const Fix<F>& fixed) -> const F<Fix<F> >& {
    return fixed.inner;
}

/** Unwrap an owned (rvalue) fixed-point value, exposing the layer as an
 * rvalue so a consuming caller can move parts out of it. */
template <template <typename> class F>
constexpr auto unwrap_fix(Fix<F>&& fixed) -> F<Fix<F> >&& {
    return std::move(fixed.inner);
}
// 53775b7e-8a78-4b79-885b-046f6232d7a3 end

} // namespace beman::tree_algorithms

#endif // BEMAN_TREE_ALGORITHMS_USE_MODULES() &&
       // !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_TREE_ALGORITHMS_FIX_HPP
