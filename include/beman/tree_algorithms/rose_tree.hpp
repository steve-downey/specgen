// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_TREE_ALGORITHMS_ROSE_TREE_HPP
#define BEMAN_TREE_ALGORITHMS_ROSE_TREE_HPP

#include <beman/tree_algorithms/config.hpp>

#if BEMAN_TREE_ALGORITHMS_USE_MODULES() && !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

import beman.tree_algorithms;

#else

    #include <beman/tree_algorithms/fix.hpp>
    #include <beman/tree_algorithms/fold_map_lookup.hpp>
    #include <beman/tree_algorithms/functor.hpp>

    #if !BEMAN_TREE_ALGORITHMS_USE_MODULES()
        #include <functional>
        #include <type_traits>
        #include <utility>
        #include <vector>
    #endif

namespace beman::tree_algorithms {

// A third shape of tree: a value and arbitrarily many children.
//
// RoseF<T, A> is the base functor of the rose tree (multiway tree) — one
// value plus a vector of child positions. Where BinaryTree and the
// fringe tree were pre-existing representations adapted to the verbs,
// the rose tree is Fix-native: Fix<RoseF> IS the representation, there
// is no separate class to adapt, and no Box either — std::vector both
// supports incomplete element types and supplies the indirection Box
// exists to provide, so the recursive knot ties without extra plumbing.
// (Box would return if the children were a fixed-arity aggregate; the
// choice is per-layer, not global.)
//
// The rose tree earns its place in the representation matrix by what it
// removes: with a variable number of children there is no "between the
// children" position, so IN-ORDER TRAVERSAL DOES NOT EXIST for this
// shape. In-order is a binary tree's contract, not a property of trees.
// This is why the elementwise machinery leaves traversal order to each
// layer's registered fold (layer_fold_typeclass) instead of imposing a
// generic order: the generic thing is the fold, the order is the
// instance's. The rose layer fold below documents and implements this
// representation's contract — pre-order, value before children, children
// left to right.

// ---------------------------------------------------------------------
// The base functor, its Layer binder, and the Fix form.
// ---------------------------------------------------------------------

// 8f0e2d0a-5c1f-4be4-9f1e-3b7a8c92d461
/** One layer of a rose (multiway) tree: a value plus zero or more child
 * positions in left-to-right order. No Box: vector supports incomplete
 * element types and already owns through indirection.
 * @tparam T element type stored at every node
 * @tparam A recursive position placeholder (not yet fixed)
 */
template <typename T, typename A>
struct RoseF {
    T              value;
    std::vector<A> children;
};

/** Binds the element type of RoseF, leaving the unary-in-A alias
 * template @c F that Fix and the verbs require. */
template <typename T>
struct RoseLayer {
    template <typename A>
    using F = RoseF<T, A>;
};

/** The Fix form of a rose tree over @p T — here the representation
 * itself, not a conversion target. */
template <typename T>
using RoseTreeFix = Fix<RoseLayer<T>::template F>;

/** Assemble a rose tree node from a value and its subtrees. */
template <typename T>
constexpr auto rose(T value, std::vector<RoseTreeFix<T> > children = {}) -> RoseTreeFix<T> {
    return wrap_fix<RoseLayer<T>::template F>(RoseF<T, RoseTreeFix<T> >{std::move(value), std::move(children)});
}
// 8f0e2d0a-5c1f-4be4-9f1e-3b7a8c92d461 end

// ---------------------------------------------------------------------
// Functor instance.
// ---------------------------------------------------------------------

// 0d3c6e8b-92f4-45f7-8a26-6c1de5b0a973
/** Functor primitive for RoseF<T, A>: applies @p fn to every child
 * position, left to right; the node value copies across untouched. */
template <typename T, typename A>
struct RoseFFunctorImpl {
    template <typename Fn>
    constexpr auto fmap(this auto&&, Fn&& fn, const RoseF<T, A>& layer) {
        using B = std::remove_cvref_t<std::invoke_result_t<Fn, const A&> >;
        std::vector<B> children;
        children.reserve(layer.children.size());
        for (const A& child : layer.children) {
            children.push_back(std::invoke(fn, child));
        }
        return RoseF<T, B>{layer.value, std::move(children)};
    }
};

/** Functor map for RoseF<T, A>: the fmap primitive plus the derived
 * operations from the Functor CRTP base. */
template <typename T, typename A>
struct RoseFFunctorMap : Functor<RoseFFunctorImpl<T, A> > {
    using RoseFFunctorImpl<T, A>::fmap;
};

/** Registers RoseFFunctorMap as the Functor instance for RoseF<T, A>. */
template <typename T, typename A>
inline constexpr auto functor_typeclass<RoseF<T, A> > = RoseFFunctorMap<T, A>{};
// 0d3c6e8b-92f4-45f7-8a26-6c1de5b0a973 end

// ---------------------------------------------------------------------
// Elementwise layer fold (for fold_map).
// ---------------------------------------------------------------------

// 4b9d17f2-30a5-4e1c-b8d4-7f52a6c3e08d
/** Folds one RoseF layer elementwise. This representation's contractual
 * order is PRE-ORDER: the mapped node value first, then each child's
 * already-folded result, left to right. There is no in-order alternative
 * to contract for — with n children there is no single "between the
 * children" slot — which is the concrete demonstration that traversal
 * order belongs to the instance, not to the generic fold. The identity
 * parameter never contributes here: a rose layer has no absent
 * positions (a leaf is simply a node with zero children).
 */
struct RoseLayerFoldMap {
    template <typename MapFn, typename Combine, typename Result, typename T>
    constexpr auto operator()(const MapFn&                   map_fn,
                              const Combine&                 combine,
                              [[maybe_unused]] const Result& identity,
                              const RoseF<T, Result>&        layer) const -> Result {
        Result acc = map_fn(layer.value);
        for (const Result& child : layer.children) {
            acc = combine(acc, child);
        }
        return acc;
    }
};

inline constexpr RoseLayerFoldMap rose_layer_fold_map{};

/** Lookup registration: the layer fold keyed on the layer type. There is
 * no project_typeclass registration — the rose tree is Fix-native, and
 * the Fix overloads of the verbs already know how to unwrap it. */
template <typename T, typename A>
inline constexpr auto layer_fold_typeclass<RoseF<T, A> > = rose_layer_fold_map;
// 4b9d17f2-30a5-4e1c-b8d4-7f52a6c3e08d end

} // namespace beman::tree_algorithms

#endif // BEMAN_TREE_ALGORITHMS_USE_MODULES() &&
       // !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_TREE_ALGORITHMS_ROSE_TREE_HPP
