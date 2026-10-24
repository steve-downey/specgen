// include/beman/specgen/backend/render_fold.hpp                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — the base functor a backend's render fold runs over (decision
// backend-direct-algebra): `ir::Node`'s shape, with a section's header
// already rendered during descent.
#ifndef BEMAN_SPECGEN_BACKEND_RENDER_FOLD_HPP
#define BEMAN_SPECGEN_BACKEND_RENDER_FOLD_HPP

#include <beman/specgen/foundation/overloaded.hpp>
#include <beman/specgen/ir.hpp>

#include <ranges>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace beman::specgen::backend {

// --- the rendering base functor (decision backend-direct-algebra) ----------
//
// A backend's render fold carries an inherited attribute -- section depth --
// that `ir::NodeF` (ir_fold.hpp) has no slot for, because `ir::NodeF` is the
// shape a fold with *no* inherited attribute wants: the validator, for
// instance, folds `ir::Node` directly via `node_project`/`node_fmap`, and a
// diagnostic has no header text that needed rendering early. A render fold
// is different: a Section's own header depends on which depth its *parent*
// assigned it, so a backend's projection renders that header during descent
// (before this layer is ever built) and needs somewhere honest to put it.
// Smuggling it into `ir::SectionF::stable_name` -- a field named for a
// different, frozen meaning -- is deliberately rejected: every backend that
// copies this pattern (mpark and org do) would inherit
// a `stable_name` that sometimes isn't one. `RenderedSectionF` is the
// honestly-named layer instead: `header` is whatever text a backend's own
// projection already rendered for this node (LaTeX's `\rSecN[...]{...}`, or
// another backend's own section-opening syntax); `children` are the
// not-yet-folded child seeds, exactly like `SectionF::children`.
//
// Pick `RenderF`/`RenderedSectionF` for a fold that resolves an inherited
// attribute during descent; pick `ir::NodeF`/`node_project` for one that
// does not.
template <typename A>
struct RenderedSectionF {
    std::string    header;
    std::vector<A> children;
};

template <typename A>
using RenderF = std::variant<RenderedSectionF<A>, ir::Synopsis, ir::SpecItem, ir::FreeParagraph>;

namespace render_fold_detail {

// `render_fmap`'s std::visit dispatch (decision visitation-rules: named
// struct, RenderF has four
// alternatives; the mapping function is member state, per the rule, rather
// than a lambda capture) -- mirrors ir_fold.hpp's NodeFMapper exactly.
template <typename Fn, typename A, typename B>
struct RenderFMapper {
    const Fn& fn;

    // The one recursive alternative: map every child handle through `fn`;
    // the already-rendered header passes through untouched.
    RenderF<B> operator()(const RenderedSectionF<A>& s) const {
        return RenderedSectionF<B>{
            s.header,
            s.children | std::views::transform([this](const A& child) { return fn(child); }) |
                std::ranges::to<std::vector>(),
        };
    }

    // Three leaves: no recursive position, passed through unchanged.
    RenderF<B> operator()(const ir::Synopsis& v) const { return v; }
    RenderF<B> operator()(const ir::SpecItem& v) const { return v; }
    RenderF<B> operator()(const ir::FreeParagraph& v) const { return v; }
};

} // namespace render_fold_detail

/// (Fn, RenderF<A>) -> RenderF<B>: maps only `RenderedSectionF::children`,
/// the one recursive position; the three leaf alternatives pass through
/// untouched. Mirrors `ir_fold.hpp`'s `node_fmap`: a generic-lambda *value*,
/// not a function template, so it can be named directly as `fold_with`'s
/// `fmap_fn` argument (a bare function template cannot be deduced as that
/// parameter with no fixed instantiation; a concrete closure type whose call
/// operator is itself a template can).
inline constexpr auto render_fmap = []<typename A>(const auto& fn, const RenderF<A>& layer) {
    using Fn = std::decay_t<decltype(fn)>;
    using B  = std::decay_t<std::invoke_result_t<const Fn&, const A&>>;
    return std::visit(beman::specgen::foundation::overloaded{render_fold_detail::RenderFMapper<Fn, A, B>{fn}}, layer);
};

} // namespace beman::specgen::backend

#endif // BEMAN_SPECGEN_BACKEND_RENDER_FOLD_HPP
