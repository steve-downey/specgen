// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_TREE_ALGORITHMS_CHILD_SLOT_HPP
#define BEMAN_TREE_ALGORITHMS_CHILD_SLOT_HPP

#include <beman/tree_algorithms/config.hpp>

#if BEMAN_TREE_ALGORITHMS_USE_MODULES() && !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

import beman.tree_algorithms;

#else

    #include <beman/tree_algorithms/box.hpp>
    #include <beman/tree_algorithms/fix.hpp>

    #if !BEMAN_TREE_ALGORITHMS_USE_MODULES()
        #include <optional>
        #include <type_traits>
        #include <utility>
    #endif

namespace beman::tree_algorithms {

// Child storage for base-functor layers: indirection exactly at the knot.
//
// A base functor F needs indirection at its recursive positions only when
// they hold the fixed point itself: F<Fix<F>> contains children of an
// incomplete type, so those must sit behind a pointer (Box). Every other
// instantiation of F — the F<Result> a fold hands to its algebra, the
// F<Seed> a coalgebra produces — is at a complete type, and its children
// can live inline on the stack. A layer type that hardwires Box pays one
// heap allocation per child per node in every materialized layer, for
// nothing the algorithm needs; measured on a 2^20-node fold, that
// allocation is the entire difference between fold_fix and hand-written
// recursion.
//
// child_slot chooses per position: Box at Fix<F> (matched by partial
// specialization, which needs only Fix's declaration, not completeness),
// std::optional everywhere else. Both are nullable, contextually
// convertible to bool, and dereference with *, so layer code reads the
// same either way; make_slot constructs whichever the position requires.

// 9dd7168f-4960-40f1-9366-db7d88caec2c
/** Child storage for one recursive position of a base-functor layer:
 * Box (indirect) exactly when the position holds the fixed point, whose
 * type is incomplete inside its own layer; std::optional (inline)
 * for every complete type — fold results, coalgebra seeds, projection
 * handles. Both are nullable, test engaged as bool, and dereference
 * with *.
 * @tparam A the type in the recursive position
 */
template <typename A>
struct child_slot {
    using type = std::optional<A>;
};

/** The knot: inside F<Fix<F>>, Fix<F> is incomplete, so the position
 * must be an indirection. */
template <template <typename> class F>
struct child_slot<Fix<F> > {
    using type = Box<Fix<F> >;
};

template <typename A>
using child_slot_t = typename child_slot<A>::type;

/** Construct an engaged child slot for @p A, forwarding @p args to A's
 * constructor: a Box for the knot, an engaged optional otherwise. A
 * default-constructed child_slot_t<A> is the disengaged (absent) child
 * either way. */
template <typename A, typename... Args>
constexpr auto make_slot(Args&&... args) -> child_slot_t<A> {
    if constexpr (std::is_same_v<child_slot_t<A>, Box<A> >) {
        return make_box<A>(std::forward<Args>(args)...);
    } else {
        return std::optional<A>(std::in_place, std::forward<Args>(args)...);
    }
}
// 9dd7168f-4960-40f1-9366-db7d88caec2c end

} // namespace beman::tree_algorithms

#endif // BEMAN_TREE_ALGORITHMS_USE_MODULES() &&
       // !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_TREE_ALGORITHMS_CHILD_SLOT_HPP
