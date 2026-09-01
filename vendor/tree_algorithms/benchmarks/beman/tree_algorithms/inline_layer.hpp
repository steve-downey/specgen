// benchmarks/beman/tree_algorithms/inline_layer.hpp                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef BEMAN_TREE_ALGORITHMS_BENCH_INLINE_LAYER_HPP
#define BEMAN_TREE_ALGORITHMS_BENCH_INLINE_LAYER_HPP

// The zero-allocation evaluated layer, and the observation motivating it.
//
// The base functor's boxed children exist to tie the recursive knot:
// F<Fix<F>> holds children of an incomplete type, so the recursive
// position must be an indirection (Box, unique_ptr, ...). But the layers
// the verbs *materialize* — the F<Result> handed to an algebra, the
// F<Seed> a coalgebra produces — instantiate F at complete types, where
// no indirection is needed at all. Every make_box/make_unique in an fmap
// over such a layer is allocation the algorithm never required; children
// of a complete type can live inline on the stack.
//
// InlineLayer<T, B> is that representation: one layer of a
// value-at-every-node binary tree, children held in std::optional —
// engaged/absent exactly as a Box is engaged/absent, no heap. For
// complete B it is isomorphic to BinaryTreeF<T, B> (the iso unboxes).
// The benchmarks thread it through the explicit-fmap verbs to measure
// what boxed materialization costs, with no library change.

#include <optional>
#include <type_traits>
#include <utility>

namespace bench {

template <typename T, typename B>
struct InlineLayer {
    T                value;
    std::optional<B> left;
    std::optional<B> right;
};

/** fmap for InlineLayer: apply fn to each engaged child, left before
 * right; absent children stay absent; no allocation anywhere. */
struct InlineLayerFMap {
    template <typename Fn, typename T, typename A>
    constexpr auto operator()(Fn&& fn, const InlineLayer<T, A>& layer) const {
        using B = std::remove_cvref_t<std::invoke_result_t<Fn&, const A&> >;
        return InlineLayer<T, B>{
            layer.value,
            layer.left ? std::optional<B>(fn(*layer.left)) : std::optional<B>{},
            layer.right ? std::optional<B>(fn(*layer.right)) : std::optional<B>{},
        };
    }
};

inline constexpr InlineLayerFMap inline_layer_fmap{};

} // namespace bench

#endif // BEMAN_TREE_ALGORITHMS_BENCH_INLINE_LAYER_HPP
