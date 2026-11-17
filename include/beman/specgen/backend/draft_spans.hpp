// include/beman/specgen/backend/draft_spans.hpp                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — the working draft's own LaTeX spelling of a span and of a code
// inline, shared by the two backends that write draft LaTeX: latex, and org
// inside its code blocks and export snippets.
#ifndef BEMAN_SPECGEN_BACKEND_DRAFT_SPANS_HPP
#define BEMAN_SPECGEN_BACKEND_DRAFT_SPANS_HPP

#include <beman/specgen/backend/code_spans.hpp>
#include <beman/specgen/foundation/enumeration.hpp>
#include <beman/specgen/ir.hpp>

#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>

namespace beman::specgen::backend {

// --- the draft's own LaTeX spelling of a span ------------------------------
//
// The mirror image of `element_label` (element_label.hpp), and shared for the
// mirror-image reason. That table exists because a backend that is *not* writing draft
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

// Literal code inside a text-mode macro argument (issue #248). The span
// walker applies this only to source slices, before inserting draft macros.
// Listings payloads and author-supplied @...@ escapes use the identity hook.
// The draft loads the underscore package: `_` stays literal (issue #252).
inline std::string draft_escape_code_text(std::string_view text) {
    return text | std::views::transform([](char ch) -> std::string {
               switch (ch) {
               case '&':
                   return "\\&";
               case '#':
                   return "\\#";
               case '%':
                   return "\\%";
               case '{':
                   return "\\{";
               case '}':
                   return "\\}";
               case '$':
                   return "\\$";
               case '~':
                   return "\\textasciitilde{}";
               case '^':
                   return "\\textasciicircum{}";
               case '\\':
                   return "\\textbackslash{}";
               default:
                   return std::string(1, ch);
               }
           }) |
           std::views::join | std::ranges::to<std::string>();
}

// A span's macro, in prose (`listing` false) or inside a listing's escape. The
// two differ only in which form an exposition-only name takes when the span
// does not say (`ir::default_no_correction`): the draft has a
// no-italic-correction twin of each italic macro, and a span the draft
// importer read says which one the draft wrote.
inline std::string draft_span_macro(const ir::Span& span, std::string_view spelling, bool listing) {
    const bool nc = span.no_correction.value_or(ir::default_no_correction(span.kind, listing));
    return foundation::match(
        span.kind,
        foundation::when<ir::SpanKind::ExposId>(
            [&] { return (nc ? "\\exposidnc{" : "\\exposid{") + span.payload + '}'; }),
        foundation::when<ir::SpanKind::SeeBelow>([&] { return std::string{nc ? "\\seebelownc" : "\\seebelow"}; }),
        foundation::when<ir::SpanKind::ImplDefined>([&] { return std::string{nc ? "\\impdefnc" : "\\impdef"}; }),
        // \cv{} is the one placeholder the draft gives its own macro, which the
        // draft's listings and its prose both use.
        foundation::when<ir::SpanKind::Placeholder>([&] {
            if (span.payload == "cv")
                return std::string{"\\cv{}"};
            return (nc ? "\\placeholdernc{" : "\\placeholder{") + span.payload + '}';
        }),
        // `\ref`, not `\iref`. A Ref *span* is a cross-reference
        // inside a code comment, which the draft writes bare --
        // `// \ref{optional.ctor}, constructors` -- where `\iref` is the
        // parenthesized form (`\nolinebreak\hspace{1sp}(\ref{...})`) that
        // belongs in prose. `ir::RefInline` is the prose one and
        // renders `\iref`; the two are different cross-references and
        // must not share a mapping.
        foundation::when<ir::SpanKind::Ref>([&] { return "\\ref{" + span.payload + '}'; }),
        foundation::when<ir::SpanKind::LibraryIndex>([&] {
            if (span.payload.empty())
                return std::format("\\libglobal{{{}}}", spelling);
            return std::format("\\libmember{{{}}}{{{}}}", spelling, span.payload);
        }),
        foundation::when<ir::SpanKind::CvQualifier>([&] { return "\\cvqual{" + span.payload + '}'; }),
        // The argument is the covered code, and a macro argument is text mode
        // wherever the macro sits -- in a \tcode and inside a listing's
        // `@...@` alike -- so it takes the text-mode escapes either way.
        foundation::when<ir::SpanKind::Optional>([&] { return "\\opt{" + draft_escape_code_text(spelling) + '}'; }));
}

// Outside a codeblock we are already in LaTeX, so the macro is written
// directly.
inline std::string draft_span_prose(const ir::Span& span, std::string_view spelling) {
    return draft_span_macro(span, spelling, false);
}

// Inside a codeblock the draft escapes back into LaTeX with @...@. Exposition
// identifiers use the no-correction form there unless the span says the
// draft wrote the other. Ref is a separate exception,
// and it is the draft's own, not ours. `macros.tex` sets the
// listings option `texcl=true` ("all TeX commands are available within
// comments") in the shared `\lstset`, alongside `escapechar=@` -- so it holds
// for `itemdecl` as much as for `codeblock`, and a `//` comment in either is
// *already* in TeX mode: the draft writes `// \ref{optional.ctor}` and
// `// \tcode{optional}` bare there, and reserves `@...@` for macros in code
// proper (`@\impdefnc@`). A Ref span only ever sits in a comment -- that is
// what the enumerator means -- so it is the one kind that must not be
// wrapped; wrapping it would typeset the delimiters.
//
// `draft_span_escaped` is the form *inside* an open `@...@`: what the
// codeblock form wraps, which is also what a span inside a changed run of
// code renders as, the run having opened the escape already
// (docs/plans/wording-diff.md). The exposition identifier takes the
// no-correction form there as it does in a codeblock; a Ref is the bare
// `\ref` either way.
inline std::string draft_span_escaped(const ir::Span& span, std::string_view spelling) {
    return draft_span_macro(span, spelling, true);
}

inline std::string draft_span_codeblock(const ir::Span& span, std::string_view spelling) {
    if (span.kind == ir::SpanKind::Ref)
        return draft_span_prose(span, spelling);
    return '@' + draft_span_escaped(span, spelling) + '@';
}

// --- the draft's own change marks (macros.tex) ------------------------------
//
// `\added{...}` and `\removed{...}` in prose; the same inside a code escape,
// which is where a changed run of code goes; and the `addedblock` /
// `removedblock` environments around a whole node. Shared with the org
// backend for the reason the span spellings are: inside its code blocks the
// bytes are the draft's, and its `#+begin_addedblock` is the special block
// that exports to the environment of the same name.

inline std::string draft_change_prose(ir::Change change, std::string_view text) {
    return foundation::match(
        change,
        foundation::when<ir::Change::Same>([&] { return std::string(text); }),
        foundation::when<ir::Change::Added>([&] { return "\\added{" + std::string(text) + '}'; }),
        foundation::when<ir::Change::Removed>([&] { return "\\removed{" + std::string(text) + '}'; }),
        foundation::when<ir::Change::Elided>([&] { return std::string(text); }));
}

inline std::string draft_change_codeblock(ir::Change change, std::string_view text) {
    if (change == ir::Change::Same)
        return std::string(text);
    return '@' + draft_change_prose(change, text) + '@';
}

// The environment a marked node sits in, and nothing for an unmarked one.
inline std::optional<std::string_view> draft_change_environment(ir::Change change) {
    return foundation::match(
        change,
        foundation::when<ir::Change::Same>([] { return std::optional<std::string_view>{}; }),
        foundation::when<ir::Change::Added>([] { return std::optional<std::string_view>{"addedblock"}; }),
        foundation::when<ir::Change::Removed>([] { return std::optional<std::string_view>{"removedblock"}; }),
        foundation::when<ir::Change::Elided>([] { return std::optional<std::string_view>{}; }));
}

// One of the draft's line-break hints, as the draft writes it: the macro, and
// the empty group after it when the source had one. A control word with no
// group must not run into a letter, so it takes the space TeX would discard;
// `following` is the text after the hint.
inline std::string draft_layout_hint_macro(const ir::LayoutHint& hint, std::string_view following) {
    std::string macro =
        foundation::match(hint.kind,
                          foundation::when<ir::LayoutHintKind::Break>([] { return std::string{"\\brk"}; }),
                          foundation::when<ir::LayoutHintKind::Hyphen>([] { return std::string{"\\-"}; }),
                          foundation::when<ir::LayoutHintKind::LineBreak>([] { return std::string{"\\linebreak"}; }),
                          foundation::when<ir::LayoutHintKind::NewLine>([] { return std::string{"\\newline"}; }));
    if (hint.braced)
        return macro + "{}";
    const bool letter_follows = !following.empty() && ((following.front() >= 'a' && following.front() <= 'z') ||
                                                       (following.front() >= 'A' && following.front() <= 'Z'));
    if (hint.kind != ir::LayoutHintKind::Hyphen && letter_follows)
        macro += ' ';
    return macro;
}

// The hint writer for code in prose, a \tcode: the text is LaTeX, so a hint is
// its macro wherever it sits.
inline std::string draft_layout_hint(const ir::LayoutHint& hint, std::string_view following, bool /*in_span*/) {
    return draft_layout_hint_macro(hint, following);
}

// The hint writer for a listing. A hint inside a span's payload is inside the
// span's `@...@` escape and is its macro. One in the listing's own text is
// not written: that text is verbatim, a listing breaks its lines where its
// text does, and the draft has no hint there for the importer to have read.
inline std::string draft_layout_hint_listing(const ir::LayoutHint& hint, std::string_view following, bool in_span) {
    return in_span ? draft_layout_hint_macro(hint, following) : std::string{};
}

// A placeholder name in prose, as the draft writes one: \cv{} for the one
// placeholder the draft gives its own macro (which also sets right in math),
// \cvqual for any other that stands for cv-qualifiers, \placeholder for the
// rest. Shared because the org backend writes the same text in its LaTeX
// export snippet.
inline std::string draft_placeholder(const ir::PlaceholderInline& v) {
    if (v.text == "cv" && !v.cv_qualifier)
        return "\\cv{}";
    if (v.cv_qualifier)
        return "\\cvqual{" + v.text + '}';
    return (v.no_correction ? "\\placeholdernc{" : "\\placeholder{") + v.text + '}';
}

// A concept name in prose: \libconcept, or \exposconcept for an
// exposition-only one.
inline std::string draft_concept(const ir::ConceptRef& v) {
    if (!v.exposition)
        return "\\libconcept{" + v.name + '}';
    return (v.no_correction ? "\\exposconceptnc{" : "\\exposconcept{") + v.name + '}';
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
    // A placeholder is the exception: `\placeholder` sets italic, not the code
    // font, and the draft writes `\tcode{\placeholder{x}}` for it. `\cvqual`
    // is the same italic, and `\opt` sets its argument in whatever font
    // surrounds it, so those two keep the wrapper as well.
    const auto sets_code_font = [](ir::SpanKind kind) {
        return kind != ir::SpanKind::Placeholder && kind != ir::SpanKind::CvQualifier &&
               kind != ir::SpanKind::Optional;
    };
    const bool whole_span = v.code.spans.size() == 1 && v.code.spans.front().begin == 0 &&
                            v.code.spans.front().end == v.code.text.size() &&
                            sets_code_font(v.code.spans.front().kind);
    // A prose code inline takes no change mark of its own (the mark is on the
    // inline), so the run hooks are the plain ones. The walk is taken for a
    // whole-span inline too: it is what writes a line-break hint inside the
    // span's name, and around one span it yields the bare macro.
    const std::string code = render_code_spans(
        v.code,
        draft_span_prose,
        draft_span_prose,
        [](ir::Change, std::string_view text) { return std::string(text); },
        draft_escape_code_text,
        draft_layout_hint);
    return whole_span ? code : "\\tcode{" + code + '}';
}

} // namespace beman::specgen::backend

#endif // BEMAN_SPECGEN_BACKEND_DRAFT_SPANS_HPP
