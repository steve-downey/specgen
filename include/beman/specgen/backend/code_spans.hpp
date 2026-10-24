// include/beman/specgen/backend/code_spans.hpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — rendering an `ir::CodeText` (decision backend-direct-algebra).
// Every backend renders one the same way: the author's own tokens pass
// through untouched, and only the byte-ranged spans are substituted, each with
// a backend-specific escape (LaTeX's `\exposid{...}` in prose and
// `@\exposidnc{...}@` in code, the mpark backend's own convention, ...). That
// walk is written once here rather than once per backend; a backend supplies
// only `escape_span`, the one place its own macro/escape spelling lives.
#ifndef BEMAN_SPECGEN_BACKEND_CODE_SPANS_HPP
#define BEMAN_SPECGEN_BACKEND_CODE_SPANS_HPP

#include <beman/specgen/ir.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace beman::specgen::backend {

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

} // namespace beman::specgen::backend

#endif // BEMAN_SPECGEN_BACKEND_CODE_SPANS_HPP
