// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_TREE_ALGORITHMS_FUNCTORS_HPP
#define BEMAN_TREE_ALGORITHMS_FUNCTORS_HPP

#include <beman/tree_algorithms/config.hpp>

#if BEMAN_TREE_ALGORITHMS_USE_MODULES() && !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

import beman.tree_algorithms;

#else

    #include <beman/tree_algorithms/box.hpp>
    #include <beman/tree_algorithms/child_slot.hpp>
    #include <beman/tree_algorithms/fix.hpp>
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

// Reusable demo base functors beyond the expression tree. Currently the
// NatF family: unary (Peano) naturals, the smallest recursive datatype
// with a genuinely recursive position, used by the paper and tests to
// demonstrate unfold_fix and refold. Layout is types -> functor_typeclass
// instance -> smart constructors/converters, so the partial specialization
// is visible before any function here that dispatches through the lookup
// verbs for NatF layers.

// ---------------------------------------------------------------------
// NatF — unary naturals.
// ---------------------------------------------------------------------

// 7970dd71-b5dd-4fa0-8e80-735a26833b65
/** Zero alternative: no recursive positions. */
struct Zero {};

/** Successor alternative: holds the predecessor in a child slot — boxed
 * at the knot, inline at complete types. */
template <typename A>
struct Succ {
    child_slot_t<A> pred;
};

/** Non-recursive naturals base functor. Nat = Fix<NatF> ties the knot. */
template <typename A>
using NatF = std::variant<Zero, Succ<A> >;
// 7970dd71-b5dd-4fa0-8e80-735a26833b65 end

/** The fixed-point naturals type. */
using Nat = Fix<NatF>;

// ---------------------------------------------------------------------
// Functor instance.
// ---------------------------------------------------------------------

// edbdeea5-9b3e-4d9a-88d5-fb2610c9174d
/** Functor primitive for NatF<A>: applies @p fn to the predecessor of a
 * Succ layer; Zero has no recursive positions. */
template <typename A>
struct NatFFunctorImpl {
    template <typename Fn>
    constexpr auto fmap(this auto&&, Fn&& fn, const NatF<A>& layer) {
        using B = std::remove_cvref_t<std::invoke_result_t<Fn, const A&> >;
        return std::visit(
            overloaded{
                [](const Zero&) -> NatF<B> { return Zero{}; },
                [&fn](const Succ<A>& s) -> NatF<B> { return Succ<B>{make_slot<B>(std::invoke(fn, *s.pred))}; },
            },
            layer);
    }
};

/** Functor map for NatF<A>: the fmap primitive plus the derived
 * operations from the Functor CRTP base. */
template <typename A>
struct NatFFunctorMap : Functor<NatFFunctorImpl<A> > {
    using NatFFunctorImpl<A>::fmap;
};

/** Registers NatFFunctorMap as the Functor instance for NatF<A>. */
template <typename A>
inline constexpr auto functor_typeclass<NatF<A> > = NatFFunctorMap<A>{};
// edbdeea5-9b3e-4d9a-88d5-fb2610c9174d end

// ---------------------------------------------------------------------
// Smart constructors and converters.
// ---------------------------------------------------------------------

// f419286e-ec56-4291-9f09-d5528685805f
/** Build the Nat zero. */
constexpr auto make_zero() -> Nat { return wrap_fix<NatF>(NatF<Nat>{Zero{}}); }

/** Wrap @p n as the predecessor of a new Succ node. */
constexpr auto make_succ(Nat n) -> Nat { return wrap_fix<NatF>(NatF<Nat>{Succ<Nat>{make_slot<Nat>(std::move(n))}}); }

/** Unfold: build a Nat counting down from @p n. The seed layer's
 * predecessor is a complete int, so its slot is inline — the coalgebra
 * allocates nothing. */
constexpr auto nat_from_int(int n) -> Nat {
    return unfold_fix<NatF>(
        [](int m) -> NatF<int> {
            if (m <= 0) {
                return Zero{};
            }
            return Succ<int>{make_slot<int>(m - 1)};
        },
        n);
}

/** Fold: count the Succ layers of @p nat. */
constexpr auto nat_to_int(const Nat& nat) -> int {
    return fold_fix<int>(
        [](const NatF<int>& layer) -> int {
            return std::visit(overloaded{
                                  [](const Zero&) { return 0; },
                                  [](const Succ<int>& s) { return *s.pred + 1; },
                              },
                              layer);
        },
        nat);
}
// f419286e-ec56-4291-9f09-d5528685805f end

} // namespace beman::tree_algorithms

#endif // BEMAN_TREE_ALGORITHMS_USE_MODULES() &&
       // !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_TREE_ALGORITHMS_FUNCTORS_HPP
