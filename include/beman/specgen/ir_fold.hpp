// include/beman/specgen/ir_fold.hpp                                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The base functor for ir::Node (decision node-base-functor). `ir.hpp`
// stays byte-identical -- it is the serialization contract the front end
// builds against -- so the recursion scheme lives here instead: `NodeF<A>` is
// `Node`'s shape with its one recursive alternative's child vector
// abstracted to an explicit handle type `A`. `node_project`/`node_embed`
// convert between `Node` and one `NodeF` layer; `node_fmap` is the functor's
// `fmap` over that one recursive slot (`SectionF::children`); the three leaf
// alternatives (`Synopsis`, `SpecItem`, `FreeParagraph`) carry no recursive
// position and pass through unchanged.
//
// Only `SectionF` needs the recursion slot, and `std::vector` supplies the
// indirection, so there is no `Box`, no `child_slot_t`, no `std::indirect`
// here -- the rose-tree treatment `tree_algorithms` documents, applied
// directly to `ir::Node`.
//
// Consumers fold a `Node` with the vendored
// `beman::tree_algorithms::fold_with<Result>(algebra, node_fmap,
// node_project, node)` -- no `Fix<F>` is materialized, matching the decision's "fold a
// tree in its own representation" mandate. `ir::Document` is a forest, not a
// single tree: fold each root in `Document::nodes` and combine (see
// `ir.cpp`'s `emit_json(Document&)`, which still walks `Document::nodes`
// directly -- `Document` itself has no `NodeF`).
//
// Per decision visitation-rules, `NodeF` has four alternatives (above the
// <=3-stateless-lambda threshold), so every `std::visit` here dispatches through a named visitor
// struct, one doc-commented case per alternative, rather than an
// `overloaded`-lambda set.
#ifndef BEMAN_SPECGEN_IR_FOLD_HPP
#define BEMAN_SPECGEN_IR_FOLD_HPP

#include <beman/specgen/foundation/overloaded.hpp>
#include <beman/specgen/ir.hpp>

#include <functional>
#include <ranges>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace beman::specgen::ir {

/// One layer of `ir::Node` with its recursive slot (`Section::children`)
/// abstracted to handle type `A`. Three alternatives are leaves and pass
/// through `node_fmap` unchanged; only `SectionF` carries children.
template <typename A>
struct SectionF {
    std::string    stable_name;
    std::string    title;
    std::vector<A> children;
};

template <typename A>
using NodeF = std::variant<SectionF<A>, Synopsis, SpecItem, FreeParagraph>;

namespace fold_detail {

/// `node_project`'s `std::visit` dispatch (decision visitation-rules: named
/// struct, `Node` has four alternatives).
struct NodeProjector {
    using Handle = std::reference_wrapper<const Node>;

    // The recursive alternative: children become cheap const-ref handles
    // that `node_project` itself accepts back on recursion (the handle
    // contract `fold_with` documents; see the toy-Rose precedent in
    // `foundation/recursion_schemes.test.cpp`).
    NodeF<Handle> operator()(const Section& s) const {
        return SectionF<Handle>{
            s.stable_name,
            s.title,
            s.children | std::views::transform([](const Node& child) { return Handle(child); }) |
                std::ranges::to<std::vector>(),
        };
    }

    // A synopsis has no recursive position: wrapped into NodeF unchanged.
    NodeF<Handle> operator()(const Synopsis& v) const { return v; }

    // A declared item has no recursive position: wrapped into NodeF
    // unchanged.
    NodeF<Handle> operator()(const SpecItem& v) const { return v; }

    // A free paragraph has no recursive position: wrapped into NodeF
    // unchanged.
    NodeF<Handle> operator()(const FreeParagraph& v) const { return v; }
};

/// `node_embed`'s `std::visit` dispatch (decision visitation-rules: named
/// struct, `NodeF` has four alternatives). Consumes its argument by move,
/// `node_project`'s dual.
struct NodeEmbedder {
    Node operator()(SectionF<Node>&& s) const {
        return Section{std::move(s.stable_name), std::move(s.title), std::move(s.children)};
    }
    Node operator()(Synopsis&& v) const { return v; }
    Node operator()(SpecItem&& v) const { return v; }
    Node operator()(FreeParagraph&& v) const { return v; }
};

/// `node_fmap`'s `std::visit` dispatch (decision visitation-rules: named
/// struct; the mapping function is member state, per the rule, rather than a
/// lambda capture).
template <typename Fn, typename A, typename B>
struct NodeFMapper {
    const Fn& fn;

    // The one recursive alternative: map every child handle through `fn`,
    // value untouched.
    NodeF<B> operator()(const SectionF<A>& s) const {
        return SectionF<B>{
            s.stable_name,
            s.title,
            s.children | std::views::transform([this](const A& child) { return fn(child); }) |
                std::ranges::to<std::vector>(),
        };
    }

    // Three leaves: no recursive position, passed through unchanged.
    NodeF<B> operator()(const Synopsis& v) const { return v; }
    NodeF<B> operator()(const SpecItem& v) const { return v; }
    NodeF<B> operator()(const FreeParagraph& v) const { return v; }
};

} // namespace fold_detail

/// Tree -> F<Handle>: exposes one layer of a `Node`, recursive positions as
/// cheap const-reference handles `node_project` itself accepts back on
/// recursion. `fold_with`'s `project` argument.
inline NodeF<std::reference_wrapper<const Node>> node_project(const Node& node) {
    return std::visit(foundation::overloaded{fold_detail::NodeProjector{}}, node);
}

/// F<Node> -> Node: rebuilds one layer of `Node` from a completed
/// base-functor layer. `node_project`'s dual; `unfold_with`'s `embed`
/// argument (no in-tree consumer, but part of the node-base-functor contract).
inline Node node_embed(NodeF<Node>&& layer) {
    return std::visit(foundation::overloaded{fold_detail::NodeEmbedder{}}, std::move(layer));
}

/// (Fn, F<A>) -> F<B>: maps only `SectionF::children`, the one recursive
/// position; the three leaf alternatives pass through untouched.
///
/// A generic-lambda *value*, not a function template, so it can be named
/// directly as `fold_with`'s `fmap_fn` argument the way decision
/// node-base-functor specifies --
/// `fold_with<Result>(algebra, node_fmap, node_project, node)` -- matching
/// the toy rose-tree `fmap` precedent
/// (`foundation/recursion_schemes.test.cpp`): a bare function template
/// cannot be deduced as `fold_with`'s `FMap` parameter with no fixed
/// instantiation, but a concrete closure type whose call operator is itself
/// a template can.
inline constexpr auto node_fmap = []<typename A>(const auto& fn, const NodeF<A>& layer) {
    using Fn = std::decay_t<decltype(fn)>;
    using B  = std::decay_t<std::invoke_result_t<const Fn&, const A&>>;
    return std::visit(foundation::overloaded{fold_detail::NodeFMapper<Fn, A, B>{fn}}, layer);
};

} // namespace beman::specgen::ir

#endif // BEMAN_SPECGEN_IR_FOLD_HPP
