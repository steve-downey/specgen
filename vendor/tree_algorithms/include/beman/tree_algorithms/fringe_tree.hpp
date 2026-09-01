// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_TREE_ALGORITHMS_FRINGE_TREE_HPP
#define BEMAN_TREE_ALGORITHMS_FRINGE_TREE_HPP

#include <beman/tree_algorithms/config.hpp>

#if BEMAN_TREE_ALGORITHMS_USE_MODULES() && !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

import beman.tree_algorithms;

#else

    #include <beman/tree_algorithms/fold_map_lookup.hpp>
    #include <beman/tree_algorithms/functor.hpp>
    #include <beman/tree_algorithms/overloaded.hpp>

    // <cassert> is macro-only and therefore not importable; it is included
    // unconditionally so assert works in the module build as well.
    #include <cassert>

    #if !BEMAN_TREE_ALGORITHMS_USE_MODULES()
        #include <cstddef>
        #include <functional>
        #include <memory>
        #include <optional>
        #include <type_traits>
        #include <utility>
        #include <variant>
        #include <vector>
    #endif

namespace beman::tree_algorithms {

// A second tree you don't own — and this time, no Fix at all.
//
// FringeTree<T> below is a faithful port of a pre-existing persistent
// sequence tree (smd::tree::FringeTree): values live exclusively at
// leaves, branches carry only structure plus a cached measure (the leaf
// count), concatenation is O(1) by structural sharing. Where the
// BinaryTree adapter (binary_tree.hpp) demonstrates conversion —
// to_fix/from_fix copying between representations — this adapter is
// direct-only: a projection and an embedding connect the tree to
// fold_with/unfold_with in its own representation, and no Fix form is
// provided or needed. The layer holds child handles by value (branches
// are never null), so there is no Box here either.
//
// The cached measure is the bridge to measured-sequence designs: the
// embedding below routes through branch(), which computes the measure,
// so trees built by unfold_with maintain the invariant by construction.

// ---------------------------------------------------------------------
// The existing tree, ported as-is.
// ---------------------------------------------------------------------

// 1484acca-10e3-4500-93e1-092ccec4e3f6
/** Persistent balanced binary tree representing a sequence of leaf
 * values. Empty, a single Leaf, or a Branch of two subtrees; values
 * live exclusively at leaves; branches carry a cached measure (leaf
 * count). Subtrees are held by shared_ptr, so copies are cheap and
 * concatenation shares structure.
 * @tparam T element type stored at leaves
 */
template <class T>
class FringeTree {
    struct Empty {};
    struct Leaf {
        T d_value;
    };
    struct Branch {
        std::size_t                 d_measure;
        std::shared_ptr<FringeTree> d_left;
        std::shared_ptr<FringeTree> d_right;
    };

    std::variant<Empty, Leaf, Branch> d_data;

  public:
    using value_type = T;

    /** Construct the empty tree (no elements). */
    static auto empty() -> FringeTree { return FringeTree(Empty{}); }

    /** Construct a single-element tree containing @p value. */
    static auto leaf(T value) -> FringeTree { return FringeTree(Leaf{std::move(value)}); }

    /** Construct a branch joining two non-empty subtrees; the measure is
     * computed here, so it is correct by construction. */
    static auto branch(FringeTree left, FringeTree right) -> FringeTree {
        auto left_ptr  = std::make_shared<FringeTree>(std::move(left));
        auto right_ptr = std::make_shared<FringeTree>(std::move(right));
        auto measure   = left_ptr->measure() + right_ptr->measure();
        return FringeTree(Branch{measure, std::move(left_ptr), std::move(right_ptr)});
    }

    /** True when the tree is empty. */
    auto is_empty() const -> bool { return std::holds_alternative<Empty>(d_data); }
    /** True when the tree is a single leaf. */
    auto is_leaf() const -> bool { return std::holds_alternative<Leaf>(d_data); }
    /** True when the tree is an internal branch. */
    auto is_branch() const -> bool { return std::holds_alternative<Branch>(d_data); }

    /** Number of leaf elements in the tree (cached at branches). */
    auto measure() const -> std::size_t {
        if (is_empty()) {
            return 0U;
        }
        if (is_leaf()) {
            return 1U;
        }
        return std::get<Branch>(d_data).d_measure;
    }

    /** Return the leaf value; precondition: is_leaf(). */
    auto value() const -> const T& {
        assert(is_leaf());
        return std::get<Leaf>(d_data).d_value;
    }

    /** Return the left subtree; precondition: is_branch(). */
    auto left() const -> const FringeTree& {
        assert(is_branch());
        return *std::get<Branch>(d_data).d_left;
    }

    /** Return the right subtree; precondition: is_branch(). */
    auto right() const -> const FringeTree& {
        assert(is_branch());
        return *std::get<Branch>(d_data).d_right;
    }

    /** Collect all leaf values into a vector in left-to-right order. */
    auto flatten() const -> std::vector<T> {
        if (is_empty()) {
            return {};
        }
        if (is_leaf()) {
            return {value()};
        }
        auto l = left().flatten();
        auto r = right().flatten();
        l.insert(l.end(), r.begin(), r.end());
        return l;
    }

    /** Concatenate two trees; empty operands are identity elements. */
    static auto concat(const FringeTree& left_tree, const FringeTree& right_tree) -> FringeTree {
        if (left_tree.is_empty()) {
            return right_tree;
        }
        if (right_tree.is_empty()) {
            return left_tree;
        }
        return branch(left_tree, right_tree);
    }

    /** Return a new tree with @p x prepended (deque cons). */
    auto cons(T x) const -> FringeTree { return concat(leaf(std::move(x)), *this); }

    /** Return a new tree with @p x appended (deque snoc). */
    auto snoc(T x) const -> FringeTree { return concat(*this, leaf(std::move(x))); }

    /** Build a tree from a vector, appending elements left-to-right. */
    static auto from_sequence(std::vector<T> values) -> FringeTree {
        auto result = empty();
        for (auto& v : values) {
            result = result.snoc(std::move(v));
        }
        return result;
    }

  private:
    explicit FringeTree(Empty e) : d_data(std::move(e)) {}
    explicit FringeTree(Leaf l) : d_data(std::move(l)) {}
    explicit FringeTree(Branch b) : d_data(std::move(b)) {}
};
// 1484acca-10e3-4500-93e1-092ccec4e3f6 end

// ---------------------------------------------------------------------
// Views: decomposition at either end, in Paper C's vocabulary.
// ---------------------------------------------------------------------

// Evidence, not (yet) proposed surface: these mirror the FingerTree
// interface of Paper C — view_l/view_r returning an optional View of the
// end element and the rest — spelled as free functions so the ported
// class above stays byte-identical to its anchored form. The contract
// is deliberately a QUOTIENT contract: the rest's element sequence is
// specified, its shape is not. A representation is free to rebalance
// internally (FingerTree does, aggressively); nothing observable through
// views, concat, or the elementwise folds can tell. Sequence-generic
// algorithms — cons/snoc, from_range, fold_left, sequence equality —
// derive from empty/leaf/concat plus the views alone; see
// examples/sequence_algorithms.cpp.

// f4f21f0a-6b7e-4f0d-9a3c-58f1c2d7ab26
/** One step of end-wise decomposition: the end element and the rest of
 * the sequence. Field names follow FingerTree::View (Paper C). */
template <typename T>
struct FringeView {
    T             d_value;
    FringeTree<T> d_rest;
};

/** Decompose at the left end: the first element and the rest. The rest's
 * sequence is flatten(tree) minus its head; its shape is unspecified.
 * Empty tree yields nullopt. */
template <typename T>
auto view_l(const FringeTree<T>& tree) -> std::optional<FringeView<T> > {
    if (tree.is_empty()) {
        return std::nullopt;
    }
    if (tree.is_leaf()) {
        return FringeView<T>{tree.value(), FringeTree<T>::empty()};
    }
    auto sub = view_l(tree.left());
    if (!sub) {
        return view_l(tree.right());
    }
    sub->d_rest = FringeTree<T>::concat(sub->d_rest, tree.right());
    return sub;
}

/** Decompose at the right end: the last element and the rest. */
template <typename T>
auto view_r(const FringeTree<T>& tree) -> std::optional<FringeView<T> > {
    if (tree.is_empty()) {
        return std::nullopt;
    }
    if (tree.is_leaf()) {
        return FringeView<T>{tree.value(), FringeTree<T>::empty()};
    }
    auto sub = view_r(tree.right());
    if (!sub) {
        return view_r(tree.left());
    }
    sub->d_rest = FringeTree<T>::concat(tree.left(), sub->d_rest);
    return sub;
}
// f4f21f0a-6b7e-4f0d-9a3c-58f1c2d7ab26 end

// ---------------------------------------------------------------------
// The base functor describing one layer of that tree.
// ---------------------------------------------------------------------

// 0a841db5-7eea-4a8c-bda8-64e9c015a32e
/** One layer of a fringe tree: no elements, one element, or two child
 * handles. Children are held by value — a branch's subtrees are never
 * absent, so no Box and no null state are needed; the tree's own
 * shared_ptr provides the indirection.
 * @tparam A recursive position handle type
 */
struct FringeEmpty {};

template <typename T>
struct FringeLeaf {
    T value;
};

template <typename A>
struct FringeBranch {
    A left;
    A right;
};

template <typename T, typename A>
using FringeTreeF = std::variant<FringeEmpty, FringeLeaf<T>, FringeBranch<A> >;
// 0a841db5-7eea-4a8c-bda8-64e9c015a32e end

// ---------------------------------------------------------------------
// Functor instance.
// ---------------------------------------------------------------------

// a692305b-eb87-43fb-98a0-901a582d7ddf
/** Functor primitive for FringeTreeF<T, A>: applies @p fn to each child
 * handle of a branch, left before right; empty and leaf layers pass
 * through with the element untouched. */
template <typename T, typename A>
struct FringeTreeFFunctorImpl {
    template <typename Fn>
    constexpr auto fmap(this auto&&, Fn&& fn, const FringeTreeF<T, A>& layer) {
        using B = std::remove_cvref_t<std::invoke_result_t<Fn, const A&> >;
        return std::visit(overloaded{
                              [](const FringeEmpty&) -> FringeTreeF<T, B> { return FringeEmpty{}; },
                              [](const FringeLeaf<T>& l) -> FringeTreeF<T, B> { return FringeLeaf<T>{l.value}; },
                              [&fn](const FringeBranch<A>& b) -> FringeTreeF<T, B> {
                                  return FringeBranch<B>{std::invoke(fn, b.left), std::invoke(fn, b.right)};
                              },
                          },
                          layer);
    }
};

/** Functor map for FringeTreeF<T, A>: the fmap primitive plus the
 * derived operations from the Functor CRTP base. */
template <typename T, typename A>
struct FringeTreeFFunctorMap : Functor<FringeTreeFFunctorImpl<T, A> > {
    using FringeTreeFFunctorImpl<T, A>::fmap;
};

/** Registers FringeTreeFFunctorMap as the Functor instance for
 * FringeTreeF<T, A>. */
template <typename T, typename A>
inline constexpr auto functor_typeclass<FringeTreeF<T, A> > = FringeTreeFFunctorMap<T, A>{};
// a692305b-eb87-43fb-98a0-901a582d7ddf end

// ---------------------------------------------------------------------
// Projection and embedding: the direct verbs' two ingredients.
// ---------------------------------------------------------------------

// 321edf34-488f-4322-ac5c-320fa5c4d1fc
/** Projection for fold_with: exposes one layer of a FringeTree, children
 * as raw non-owning pointers into the tree we already have. */
struct FringeTreeProjectFn {
    template <typename T>
    auto operator()(const FringeTree<T>* t) const -> FringeTreeF<T, const FringeTree<T>*> {
        if (t->is_empty()) {
            return FringeEmpty{};
        }
        if (t->is_leaf()) {
            return FringeLeaf<T>{t->value()};
        }
        return FringeBranch<const FringeTree<T>*>{&t->left(), &t->right()};
    }
};

inline constexpr FringeTreeProjectFn fringe_tree_project{};

/** Embedding for unfold_with: rebuilds one layer in the tree's own
 * representation. Branch layers route through FringeTree::branch(),
 * which computes the cached measure — the invariant is maintained by
 * construction for every tree unfold_with builds. */
struct FringeTreeEmbedFn {
    template <typename T>
    auto operator()(FringeTreeF<T, FringeTree<T> >&& layer) const -> FringeTree<T> {
        return std::visit(overloaded{
                              [](FringeEmpty&&) { return FringeTree<T>::empty(); },
                              [](FringeLeaf<T>&& l) { return FringeTree<T>::leaf(std::move(l.value)); },
                              [](FringeBranch<FringeTree<T> >&& b) {
                                  return FringeTree<T>::branch(std::move(b.left), std::move(b.right));
                              },
                          },
                          std::move(layer));
    }
};

inline constexpr FringeTreeEmbedFn fringe_tree_embed{};
// 321edf34-488f-4322-ac5c-320fa5c4d1fc end

// ---------------------------------------------------------------------
// Elementwise layer fold (for fold_map).
// ---------------------------------------------------------------------

// e6cd1dd3-85b1-4ce2-bf7f-2ab1783d16f8
/** Folds one FringeTreeF layer elementwise, left to right. With values
 * only at leaves this is the cleanest layer fold of the shipped
 * representations: empty is the identity, a leaf is one mapped element,
 * and a branch just combines its children's results. */
struct FringeTreeLayerFoldMap {
    template <typename MapFn, typename Combine, typename Result, typename T>
    constexpr auto operator()(const MapFn&                  map_fn,
                              const Combine&                combine,
                              const Result&                 identity,
                              const FringeTreeF<T, Result>& layer) const -> Result {
        return std::visit(overloaded{
                              [&](const FringeEmpty&) -> Result { return identity; },
                              [&](const FringeLeaf<T>& l) -> Result { return map_fn(l.value); },
                              [&](const FringeBranch<Result>& b) -> Result { return combine(b.left, b.right); },
                          },
                          layer);
    }
};

inline constexpr FringeTreeLayerFoldMap fringe_tree_layer_fold_map{};

/** Lookup registrations: the projection keyed on the tree type, the
 * layer fold keyed on the layer type. */
template <typename T>
inline constexpr auto project_typeclass<FringeTree<T> > = fringe_tree_project;

template <typename T, typename A>
inline constexpr auto layer_fold_typeclass<FringeTreeF<T, A> > = fringe_tree_layer_fold_map;
// e6cd1dd3-85b1-4ce2-bf7f-2ab1783d16f8 end

} // namespace beman::tree_algorithms

#endif // BEMAN_TREE_ALGORITHMS_USE_MODULES() &&
       // !defined(BEMAN_TREE_ALGORITHMS_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_TREE_ALGORITHMS_FRINGE_TREE_HPP
