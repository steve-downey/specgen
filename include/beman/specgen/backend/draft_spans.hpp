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

// Outside a codeblock we are already in LaTeX, so the macro is written
// directly.
inline std::string draft_span_prose(const ir::Span& span, std::string_view spelling) {
    return foundation::match(
        span.kind,
        foundation::when<ir::SpanKind::ExposId>([&] { return "\\exposid{" + span.payload + '}'; }),
        foundation::when<ir::SpanKind::SeeBelow>([&] { return std::string{"\\seebelow"}; }),
        foundation::when<ir::SpanKind::ImplDefined>([&] { return std::string{"\\impdef"}; }),
        foundation::when<ir::SpanKind::Placeholder>([&] { return "\\placeholder{" + span.payload + '}'; }),
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
        }));
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

} // namespace beman::specgen::backend

#endif // BEMAN_SPECGEN_BACKEND_DRAFT_SPANS_HPP
