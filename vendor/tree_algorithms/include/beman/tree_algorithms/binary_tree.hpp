// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_TREE_ALGORITHMS_BINARY_TREE_HPP
#define BEMAN_TREE_ALGORITHMS_BINARY_TREE_HPP

#include <beman/tree_algorithms/config.hpp>

#if BEMAN_TREE_ALGORITHMS_USE_MODULES() && !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

import beman.tree_algorithms;

#else

    #include <beman/tree_algorithms/box.hpp>
    #include <beman/tree_algorithms/child_slot.hpp>
    #include <beman/tree_algorithms/fix.hpp>
    #include <beman/tree_algorithms/fold_map_lookup.hpp>
    #include <beman/tree_algorithms/functor.hpp>
    #include <beman/tree_algorithms/recursion_schemes_lookup.hpp>

    // <cassert> is macro-only and therefore not importable; it is included
    // unconditionally so assert works in the module build as well.
    #include <cassert>

    #if !BEMAN_TREE_ALGORITHMS_USE_MODULES()
        #include <functional>
        #include <memory>
        #include <type_traits>
        #include <utility>
    #endif

namespace beman::tree_algorithms {

// Adapting an existing tree you don't own.
//
// BinaryTree<T> below is a faithful port of a pre-existing persistent
// binary tree (smd::tree::BinaryTree): value at every node, children held
// by shared_ptr so copies are cheap and subtrees are structurally shared.
// That representation is part of its identity — and it is not Fix-based,
// so the recursion verbs cannot run over it directly.
//
// Design choice: rather than rewrite the tree, we adapt it. BinaryTreeF
// is the base functor describing one layer of a value-at-every-node
// binary tree (nullable child slots mirror the tree's nullable
// shared_ptr children; see child_slot.hpp — Box at the knot, inline
// storage at complete types), with a Functor instance registered for it, and
// to_fix/from_fix convert between the shared_ptr representation and
// Fix form. The same fold_fix/unfold_fix/refold that run over ExprF run
// unchanged over the converted tree; only the adapter knows how the tree
// is really stored. The conversions copy — an honest cost, consistent
// with the value-semantics stance of Decision 6.
//
// BinaryTree itself is not constexpr-capable (shared_ptr allocation is a
// runtime affair in C++23); the BinaryTreeF/Fix side is fully constexpr.

// ---------------------------------------------------------------------
// The existing tree, ported as-is.
// ---------------------------------------------------------------------

// f6f4cee4-9c55-4add-b38b-496936319294
/** Persistent binary tree where every node carries a value.
 * Nodes are either leaves (no children) or internal nodes with left and
 * right subtrees. Sharing is structural: subtrees are held by shared_ptr,
 * so copies are cheap and the tree is immutable once built.
 * @tparam T element type stored at every node
 */
template <class T>
class BinaryTree {
    T                           d_value;
    std::shared_ptr<BinaryTree> d_left;
    std::shared_ptr<BinaryTree> d_right;

  public:
    using value_type = T;

    /** Construct a leaf node holding @p value (no children). */
    static auto leaf(T value) -> BinaryTree { return BinaryTree(std::move(value), {}, {}); }

    /** Construct an internal node with @p value and two children. */
    static auto node(T value, BinaryTree left, BinaryTree right) -> BinaryTree {
        return BinaryTree(std::move(value),
                          std::make_shared<BinaryTree>(std::move(left)),
                          std::make_shared<BinaryTree>(std::move(right)));
    }

    /** Alias for node(); prefer node() in new code. */
    static auto branch(T value, BinaryTree left, BinaryTree right) -> BinaryTree {
        return node(std::move(value), std::move(left), std::move(right));
    }

    /** Low-level constructor accepting pre-built child shared_ptrs.
     * Null pointers represent absent children.
     */
    static auto from_children_ptrs(T value, std::shared_ptr<BinaryTree> left, std::shared_ptr<BinaryTree> right)
        -> BinaryTree {
        return BinaryTree(std::move(value), std::move(left), std::move(right));
    }

    /** Heap-allocate a copy of @p tree and return the owning pointer. */
    static auto make_ptr(BinaryTree tree) -> std::shared_ptr<BinaryTree> {
        return std::make_shared<BinaryTree>(std::move(tree));
    }

    /** Return the value stored at this node. */
    auto value() const -> const T& { return d_value; }

    /** True when this node has a left child. */
    auto has_left() const -> bool { return static_cast<bool>(d_left); }
    /** True when this node has a right child. */
    auto has_right() const -> bool { return static_cast<bool>(d_right); }

    /** Return the left child; precondition: has_left(). */
    auto left() const -> const BinaryTree& {
        assert(d_left);
        return *d_left;
    }

    /** Return the right child; precondition: has_right(). */
    auto right() const -> const BinaryTree& {
        assert(d_right);
        return *d_right;
    }

    /** Shared pointer to the left child; may be null. */
    auto left_ptr() const -> const std::shared_ptr<BinaryTree>& { return d_left; }
    /** Shared pointer to the right child; may be null. */
    auto right_ptr() const -> const std::shared_ptr<BinaryTree>& { return d_right; }

  private:
    BinaryTree(T value, std::shared_ptr<BinaryTree> left, std::shared_ptr<BinaryTree> right)
        : d_value(std::move(value)), d_left(std::move(left)), d_right(std::move(right)) {}
};
// f6f4cee4-9c55-4add-b38b-496936319294 end

// ---------------------------------------------------------------------
// The base functor describing one layer of that tree.
// ---------------------------------------------------------------------

// 27aeff53-a371-43b6-9037-135d5d008c26
/** One layer of a value-at-every-node binary tree: a value plus nullable
 * children in child slots. A disengaged slot mirrors an absent child,
 * exactly as a null shared_ptr does in BinaryTree. The slot is a Box
 * exactly when A is the fixed point (the knot, where A is incomplete)
 * and inline std::optional storage at every complete type — so the
 * F<Result> layers a fold materializes allocate nothing.
 * @tparam T element type stored at every node
 * @tparam A recursive position placeholder (not yet fixed)
 */
template <typename T, typename A>
struct BinaryTreeF {
    T               value;
    child_slot_t<A> left;
    child_slot_t<A> right;
};

/** Binds the element type of BinaryTreeF, leaving the unary-in-A alias
 * template @c F that Fix and the verbs require. */
template <typename T>
struct BinaryTreeLayer {
    template <typename A>
    using F = BinaryTreeF<T, A>;
};

/** The Fix form of a value-at-every-node binary tree over @p T. */
template <typename T>
using BinaryTreeFix = Fix<BinaryTreeLayer<T>::template F>;
// 27aeff53-a371-43b6-9037-135d5d008c26 end

// ---------------------------------------------------------------------
// Functor instance.
// ---------------------------------------------------------------------

// 91fef612-39b5-4424-8ab5-7d8c80997e2c
/** Functor primitive for BinaryTreeF<T, A>: applies @p fn to each engaged
 * child, left before right; absent (disengaged) children stay absent. The
 * node value copies across untouched. make_slot picks the result layer's
 * storage: inline for complete B, boxed only if B is itself the knot.
 */
template <typename T, typename A>
struct BinaryTreeFFunctorImpl {
    template <typename Fn>
    constexpr auto fmap(this auto&&, Fn&& fn, const BinaryTreeF<T, A>& layer) {
        using B = std::remove_cvref_t<std::invoke_result_t<Fn, const A&> >;
        return BinaryTreeF<T, B>{
            layer.value,
            layer.left ? make_slot<B>(std::invoke(fn, *layer.left)) : child_slot_t<B>{},
            layer.right ? make_slot<B>(std::invoke(fn, *layer.right)) : child_slot_t<B>{},
        };
    }
};

/** Functor map for BinaryTreeF<T, A>: the fmap primitive plus the derived
 * operations from the Functor CRTP base. */
template <typename T, typename A>
struct BinaryTreeFFunctorMap : Functor<BinaryTreeFFunctorImpl<T, A> > {
    using BinaryTreeFFunctorImpl<T, A>::fmap;
};

/** Registers BinaryTreeFFunctorMap as the Functor instance for
 * BinaryTreeF<T, A>. */
template <typename T, typename A>
inline constexpr auto functor_typeclass<BinaryTreeF<T, A> > = BinaryTreeFFunctorMap<T, A>{};
// 91fef612-39b5-4424-8ab5-7d8c80997e2c end

// ---------------------------------------------------------------------
// Elementwise layer fold (for fold_map).
// ---------------------------------------------------------------------

// 79aa4002-fa4a-472e-b01b-f580a13f60ec
/** Folds one BinaryTreeF layer elementwise, in order: the left child's
 * already-folded result, then the mapped node value, then the right
 * child's result. Absent children contribute the identity. In-order
 * traversal is this representation's contract; a non-commutative combine
 * observes it.
 */
struct BinaryTreeLayerFoldMap {
    template <typename MapFn, typename Combine, typename Result, typename T>
    constexpr auto operator()(const MapFn&                  map_fn,
                              const Combine&                combine,
                              const Result&                 identity,
                              const BinaryTreeF<T, Result>& layer) const -> Result {
        Result left  = layer.left ? *layer.left : identity;
        Result right = layer.right ? *layer.right : identity;
        return combine(combine(left, map_fn(layer.value)), right);
    }
};

inline constexpr BinaryTreeLayerFoldMap binary_tree_layer_fold_map{};
// 79aa4002-fa4a-472e-b01b-f580a13f60ec end

// ---------------------------------------------------------------------
// Direct-verb projection, and the lookup registrations.
// ---------------------------------------------------------------------

// c9e514d9-4cf1-4fa1-ac4d-a9dfdf040291
/** Projection for the direct verbs: exposes one layer of a BinaryTree,
 * children as raw non-owning pointers read from the shared_ptr spine in
 * place — no conversion, no structure copied beyond one layer. The
 * pointer handles are complete types, so their slots are inline: the
 * projected layer allocates nothing. */
struct BinaryTreeProjectFn {
    template <typename T>
    auto operator()(const BinaryTree<T>* t) const -> BinaryTreeF<T, const BinaryTree<T>*> {
        using P = const BinaryTree<T>*;
        return BinaryTreeF<T, P>{t->value(),
                                 t->has_left() ? make_slot<P>(&t->left()) : child_slot_t<P>{},
                                 t->has_right() ? make_slot<P>(&t->right()) : child_slot_t<P>{}};
    }
};

inline constexpr BinaryTreeProjectFn binary_tree_project{};

/** Lookup registrations: the projection keyed on the tree type, the
 * layer fold keyed on the layer type. */
template <typename T>
inline constexpr auto project_typeclass<BinaryTree<T> > = binary_tree_project;

template <typename T, typename A>
inline constexpr auto layer_fold_typeclass<BinaryTreeF<T, A> > = binary_tree_layer_fold_map;
// c9e514d9-4cf1-4fa1-ac4d-a9dfdf040291 end

// ---------------------------------------------------------------------
// Conversions between the shared_ptr tree and its Fix form.
// ---------------------------------------------------------------------

// a7ed3ac1-4c61-4ae4-a633-f26216a39ac5
/** Convert a BinaryTree<T> to its Fix form, layer by layer. Deep copy: the
 * result owns its structure; the source tree is untouched. */
template <typename T>
auto to_fix(const BinaryTree<T>& tree) -> BinaryTreeFix<T> {
    using Fixed = BinaryTreeFix<T>;
    return wrap_fix<BinaryTreeLayer<T>::template F>(
        BinaryTreeF<T, Fixed>{tree.value(),
                              tree.has_left() ? make_slot<Fixed>(to_fix(tree.left())) : child_slot_t<Fixed>{},
                              tree.has_right() ? make_slot<Fixed>(to_fix(tree.right())) : child_slot_t<Fixed>{}});
}

/** Extracts the element type from a BinaryTreeF layer type; T is a
 * non-deduced context inside BinaryTreeFix<T> (it sits behind a nested
 * alias template), so from_fix deduces the base functor and recovers the
 * element type through this trait instead. */
template <typename Layer>
struct binary_tree_element;

template <typename T, typename A>
struct binary_tree_element<BinaryTreeF<T, A> > {
    using type = T;
};

/** Rebuild a BinaryTree<T> from its Fix form — itself a fold: the algebra
 * reassembles one layer of BinaryTree from already-rebuilt children. */
template <template <typename> class F>
auto from_fix(const Fix<F>& fixed) -> BinaryTree<typename binary_tree_element<F<Fix<F> > >::type> {
    using T              = typename binary_tree_element<F<Fix<F> > >::type;
    auto rebuild_algebra = [](const BinaryTreeF<T, BinaryTree<T> >& layer) -> BinaryTree<T> {
        return BinaryTree<T>::from_children_ptrs(layer.value,
                                                 layer.left ? BinaryTree<T>::make_ptr(*layer.left) : nullptr,
                                                 layer.right ? BinaryTree<T>::make_ptr(*layer.right) : nullptr);
    };
    return fold_fix<BinaryTree<T> >(rebuild_algebra, fixed);
}
// a7ed3ac1-4c61-4ae4-a633-f26216a39ac5 end

} // namespace beman::tree_algorithms

#endif // BEMAN_TREE_ALGORITHMS_USE_MODULES() &&
       // !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_TREE_ALGORITHMS_BINARY_TREE_HPP
