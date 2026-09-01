// include/beman/specgen/backend/common.hpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — shared backend substrate (decision backend-direct-algebra).
// Every backend renders `ir::CodeText` the same way: the author's own tokens
// pass through untouched, and only the byte-ranged spans are substituted,
// each with a backend-specific escape (LaTeX's `\exposid{...}` in prose and
// `@\exposidnc{...}@` in code, the
// mpark backend's own convention, ...). That walk is written once
// here rather than once per backend; a backend supplies only `escape_span`,
// the one place its own macro/escape spelling lives.
#ifndef BEMAN_SPECGEN_BACKEND_COMMON_HPP
#define BEMAN_SPECGEN_BACKEND_COMMON_HPP

#include <beman/specgen/foundation/overloaded.hpp>
#include <beman/specgen/ir.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace beman::specgen::backend::common {

// --- the draft's own name for a description element ------------------------
//
// [structure.specifications]'s label, as the working draft renders it:
// `\effects` sets "Effects:", `\expects` sets "Preconditions:", `\errors`
// sets "Error conditions:". Read off the draft's macros.tex (each element
// macro is `\Fundesc{<label>}`), not paraphrased.
//
// This is deliberately *not* `ir::element_name`, which spells the LaTeX macro
// (`"expects"`, `"errors"`) and is simultaneously the IR's JSON schema key --
// a frozen wire spelling that must not drift into a display concern. And it
// is deliberately not in the LaTeX backend either: that backend never needs a
// label, because the macro carries it, while every backend that is *not*
// writing draft LaTeX -- mpark, org -- needs this exact table.
constexpr std::array<std::pair<ir::ElementKind, std::string_view>, 13> kElementLabels{{
    {ir::ElementKind::Constraints, "Constraints"},
    {ir::ElementKind::Mandates, "Mandates"},
    {ir::ElementKind::Expects, "Preconditions"},
    {ir::ElementKind::HardExpects, "Hardened preconditions"},
    {ir::ElementKind::Effects, "Effects"},
    {ir::ElementKind::Sync, "Synchronization"},
    {ir::ElementKind::Ensures, "Postconditions"},
    {ir::ElementKind::Result, "Result"},
    {ir::ElementKind::Returns, "Returns"},
    {ir::ElementKind::Throws, "Throws"},
    {ir::ElementKind::Complexity, "Complexity"},
    {ir::ElementKind::Remarks, "Remarks"},
    {ir::ElementKind::Errors, "Error conditions"},
}};

constexpr std::string_view element_label(ir::ElementKind kind) {
    const auto it = std::ranges::find_if(kElementLabels, [kind](const auto& entry) { return entry.first == kind; });
    return it != kElementLabels.end() ? it->second : "?";
}

// Tree -> text for one CodeText, splicing each span's backend-supplied
// escape between slices of the surrounding raw text.
//
// `escape_span` is the extension point every backend plugs into:
// `(const ir::Span&, std::string_view) -> std::string`, mapping one span and
// the covered source spelling to its backend-specific escape text. The
// spelling matters for a library-index span: its payload is the optional
// enclosing class rather than a duplicate of the printed name. LaTeX supplies
// `escape_span_prose`/`escape_span_codeblock` (`latex.cpp`); the walk itself
// never changes across backends, only this function does -- which is where
// the mpark and org backends plug their own escape conventions in,
// rather than re-deriving the span walk.
template <typename EscapeSpan>
std::string render_code_spans(const ir::CodeText& code, EscapeSpan&& escape_span) {
    std::string out;
    std::size_t pos = 0;
    // substrate generic algorithm: a position-tracking left-to-right walk
    // over a sorted, non-overlapping span table is the array-substitution
    // primitive itself, not a fold in disguise -- there is no smaller unit
    // to fold over.
    for (const auto& span : code.spans) {
        if (span.begin > code.text.size() || span.end > code.text.size() || span.begin < pos || span.end < span.begin)
            continue; // defensive: ignore a malformed or overlapping span
        out += code.text.substr(pos, span.begin - pos);
        out += escape_span(span, std::string_view(code.text).substr(span.begin, span.end - span.begin));
        pos = span.end;
    }
    out += code.text.substr(pos);
    return out;
}

// --- the draft's own LaTeX spelling of a span ------------------------------
//
// The mirror image of `element_label` above, and here for the mirror-image
// reason. That table exists because a backend that is *not* writing draft
// LaTeX has to supply the label the macro would have carried; these exist
// because a backend that *is* writing draft LaTeX has to spell the macros
// exactly as the draft does, and there are two of those.
//
// The org backend is the second, which is not obvious and is the whole
// justification for hoisting these out of `latex.cpp`: its code blocks are
// `#+begin_codeblock`/`#+begin_itemdecl` special blocks, which org's stock
// `org-latex-special-block` exports to `\begin{codeblock}`/`\begin{itemdecl}`
// -- the draft's own `lstnewenvironment`s, reaching a wg21org paper through
// `common.tex`'s `\input{stdtex/macros}`. So the bytes inside an org code
// block and inside a draft codeblock are the same bytes, produced against the
// same `escapechar=@` and `texcl=true`. A divergence between the two backends
// here would be a defect rather than a difference; copying the switch would
// have made one possible.
//
// Not `escape_span_*`, which is what `latex.cpp` called them, because the
// name now has to say *whose* convention it is rather than what it does to a
// string.

// Outside a codeblock we are already in LaTeX, so the macro is written
// directly.
inline std::string draft_span_prose(const ir::Span& span, std::string_view spelling) {
    switch (span.kind) {
    case ir::SpanKind::ExposId:
        return "\\exposid{" + span.payload + '}';
    case ir::SpanKind::SeeBelow:
        return "\\seebelow";
    case ir::SpanKind::ImplDefined:
        return "\\impdef";
    case ir::SpanKind::Placeholder:
        return "\\placeholder{" + span.payload + '}';
    case ir::SpanKind::Ref:
        // `\ref`, not `\iref`. A Ref *span* is a cross-reference
        // inside a code comment, which the draft writes bare --
        // `// \ref{optional.ctor}, constructors` -- where `\iref` is the
        // parenthesized form (`\nolinebreak\hspace{1sp}(\ref{...})`) that
        // belongs in prose. `ir::RefInline` is the prose one and
        // renders `\iref`; the two are different cross-references and
        // must not share a mapping.
        return "\\ref{" + span.payload + '}';
    case ir::SpanKind::LibraryIndex:
        if (span.payload.empty())
            return std::format("\\libglobal{{{}}}", spelling);
        return std::format("\\libmember{{{}}}{{{}}}", spelling, span.payload);
    }
    std::unreachable(); // SpanKind is exhaustively handled above
}

// Inside a codeblock the draft escapes back into LaTeX with @...@. Exposition
// identifiers use the no-correction form there. Ref is a separate exception,
// and it is the draft's own, not ours. `macros.tex` sets the
// listings option `texcl=true` ("all TeX commands are available within
// comments") in the shared `\lstset`, alongside `escapechar=@` -- so it holds
// for `itemdecl` as much as for `codeblock`, and a `//` comment in either is
// *already* in TeX mode: the draft writes `// \ref{optional.ctor}` and
// `// \tcode{optional}` bare there, and reserves `@...@` for macros in code
// proper (`@\impdefnc@`). A Ref span only ever sits in a comment -- that is
// what the enumerator means -- so it is the one kind that must not be
// wrapped; wrapping it would typeset the delimiters.
inline std::string draft_span_codeblock(const ir::Span& span, std::string_view spelling) {
    if (span.kind == ir::SpanKind::Ref)
        return draft_span_prose(span, spelling);
    if (span.kind == ir::SpanKind::ExposId)
        return "@\\exposidnc{" + span.payload + "}@";
    return '@' + draft_span_prose(span, spelling) + '@';
}

// A code inline, in prose, as the draft writes one. Shared for the same
// reason the two above are: the org backend emits this exact text inside an
// `@@latex:...@@` export snippet, because org's own `~code~` markup is
// verbatim and cannot carry a span.
//
// A code inline that is *entirely* one span renders as the bare macro: the
// draft's \exposid/\seebelow already set the code font, so wrapping them in
// \tcode would double up. Partial spans still need the wrapper
// (`x.\exposid{val}`).
inline std::string draft_code_inline(const ir::CodeInline& v) {
    const bool whole_span =
        v.code.spans.size() == 1 && v.code.spans.front().begin == 0 && v.code.spans.front().end == v.code.text.size();
    if (whole_span)
        return draft_span_prose(v.code.spans.front(), v.code.text);
    return "\\tcode{" + render_code_spans(v.code, draft_span_prose) + '}';
}

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

} // namespace beman::specgen::backend::common

#endif // BEMAN_SPECGEN_BACKEND_COMMON_HPP
