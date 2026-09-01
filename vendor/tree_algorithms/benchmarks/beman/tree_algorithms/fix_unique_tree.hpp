// benchmarks/beman/tree_algorithms/fix_unique_tree.hpp              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef BEMAN_TREE_ALGORITHMS_BENCH_FIX_UNIQUE_TREE_HPP
#define BEMAN_TREE_ALGORITHMS_BENCH_FIX_UNIQUE_TREE_HPP

// A binary-tree base functor whose recursive child positions are managed by
// std::unique_ptr, not this library's Box.
//
// The point of the comparison: Fix<F> stores F<Fix<F>> and knows nothing
// about how F holds its children. Box, std::unique_ptr, std::shared_ptr,
// std::indirect, inline storage — that is the *functor's* choice (the
// user's), orthogonal to Fix. binary_tree.hpp's BinaryTreeF picks Box at
// the knot (regular, deep-copy); UniquePtrTreeF here picks unique_ptr,
// which makes the resulting Fix tree move-only (irregular) and as cheap
// to build and fold as the hand-written unique_ptr tree. Same Fix, same
// recursion verbs; only the storage policy in the functor changes, and
// the runtime characteristics follow the storage policy, not Fix.
//
// The library's child_slot pattern generalizes to any knot storage:
// uslot below is child_slot with unique_ptr in place of Box — indirect
// exactly at the knot, inline std::optional at every complete type, so
// this functor's materialized layers are allocation-free too and the
// comparison stays purely about the knot.

#include <beman/tree_algorithms/fix.hpp>
#include <beman/tree_algorithms/functor.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace bench {

// child_slot's selection rule with unique_ptr as the knot storage.
template <typename A>
struct uslot {
    using type = std::optional<A>;
};

template <template <typename> class F>
struct uslot<beman::tree_algorithms::Fix<F> > {
    using type = std::unique_ptr<beman::tree_algorithms::Fix<F> >;
};

template <typename A>
using uslot_t = typename uslot<A>::type;

template <typename A, typename... Args>
constexpr auto make_uslot(Args&&... args) -> uslot_t<A> {
    if constexpr (std::is_same_v<uslot_t<A>, std::unique_ptr<A> >) {
        return std::make_unique<A>(std::forward<Args>(args)...);
    } else {
        return std::optional<A>(std::in_place, std::forward<Args>(args)...);
    }
}

// One layer: a value plus children in uslots — unique_ptr at the knot, a
// disengaged slot is an absent child.
template <typename T, typename A>
struct UniquePtrTreeF {
    T          value;
    uslot_t<A> left;
    uslot_t<A> right;
};

template <typename T>
struct UniquePtrTreeLayer {
    template <typename A>
    using F = UniquePtrTreeF<T, A>;
};

template <typename T>
using UniquePtrTreeFix = beman::tree_algorithms::Fix<UniquePtrTreeLayer<T>::template F>;

// Functor primitive: apply fn to each engaged child, left before right;
// absent (disengaged) children stay absent, the value copies across.
// make_uslot keeps result layers inline at complete types.
template <typename T, typename A>
struct UniquePtrTreeFFunctorImpl {
    template <typename Fn>
    constexpr auto fmap(this auto&&, Fn&& fn, const UniquePtrTreeF<T, A>& layer) {
        using B = std::remove_cvref_t<std::invoke_result_t<Fn, const A&> >;
        return UniquePtrTreeF<T, B>{
            layer.value,
            layer.left ? make_uslot<B>(std::invoke(fn, *layer.left)) : uslot_t<B>{},
            layer.right ? make_uslot<B>(std::invoke(fn, *layer.right)) : uslot_t<B>{},
        };
    }
};

template <typename T, typename A>
struct UniquePtrTreeFFunctorMap : beman::tree_algorithms::Functor<UniquePtrTreeFFunctorImpl<T, A> > {
    using UniquePtrTreeFFunctorImpl<T, A>::fmap;
};

} // namespace bench

namespace beman::tree_algorithms {

// Register the Functor instance so the lookup-based verbs (fold_fix,
// unfold_fix, refold) resolve fmap for UniquePtrTreeF exactly as they do for
// BinaryTreeF.
template <typename T, typename A>
inline constexpr auto functor_typeclass<bench::UniquePtrTreeF<T, A> > = bench::UniquePtrTreeFFunctorMap<T, A>{};

} // namespace beman::tree_algorithms

#endif // BEMAN_TREE_ALGORITHMS_BENCH_FIX_UNIQUE_TREE_HPP
