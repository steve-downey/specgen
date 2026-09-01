// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_TREE_ALGORITHMS_FUNCTOR_HPP
#define BEMAN_TREE_ALGORITHMS_FUNCTOR_HPP

#include <beman/tree_algorithms/config.hpp>

#if BEMAN_TREE_ALGORITHMS_USE_MODULES() && !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

import beman.tree_algorithms;

#else

    #if !BEMAN_TREE_ALGORITHMS_USE_MODULES()
        #include <type_traits>
        #include <utility>
    #endif

namespace beman::tree_algorithms {

// Functor-only typeclass lookup.
//
// This header is a consumer-side stub of the Paper A (P3200) bundled-
// customization facility (Decision 4): tree_algorithms consumes the lookup
// mechanism, it never respecifies it. When a library implementation of that
// facility exists, this stub is to be replaced by a dependency on it; only
// the minimum needed to dispatch fmap for base-functor layer types ships
// here.
//
// Pattern invariants (ported from the typeclass-object evidence repos):
// - Instances are single lookup objects that provide fmap(fn, value).
// - replace is a derived operation implemented from fmap in the CRTP base.
// - Dispatch happens through a provided object or functor_typeclass<T>,
//   keyed on the concrete (layer) type; partial specializations such as
//   functor_typeclass<NatF<A>> are the intended adaptation surface.
// - Lookup stays explicit through typeclass objects, not ADL overloads.

/** CRTP base for Functor instances.
 *
 * `Impl` must provide the primitive `fmap(fn, value)`; every derived
 * operation is marked constexpr at declaration time (DEV-04).
 * @tparam Impl the implementation type supplying the fmap primitive
 */
template <class Impl>
struct Functor : protected Impl {
    using Impl::fmap;

    /** Derived from fmap: replaces every element of @p value with
     * @p replacement, ignoring the original element values.
     */
    template <class T, class U>
    constexpr auto replace(this auto&& self, T&& value, U&& replacement) {
        return self.fmap([replacement = std::forward<U>(replacement)](const auto&) { return replacement; },
                         std::forward<T>(value));
    }
};

/** Typeclass lookup variable for Functor.
 *
 * Specialize (usually partially, keyed on the concrete layer type) for each
 * participating type; the unspecialized default is std::false_type so a
 * missing instance is detectable.
 */
// 5cdef435-de62-4973-91f4-a56dbef659f3
template <class T>
inline constexpr auto functor_typeclass = std::false_type{};
// 5cdef435-de62-4973-91f4-a56dbef659f3 end

/** True when functor_typeclass<T> has been specialized away from its
 * std::false_type default, i.e. T has a Functor instance.
 */
template <class T>
inline constexpr bool has_functor_instance =
    !std::is_same_v<std::remove_cvref_t<decltype(functor_typeclass<T>)>, std::false_type>;

} // namespace beman::tree_algorithms

#endif // BEMAN_TREE_ALGORITHMS_USE_MODULES() &&
       // !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_TREE_ALGORITHMS_FUNCTOR_HPP
