// include/beman/specgen/backend/code_spans.hpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — rendering an `ir::CodeText` (decision backend-direct-algebra).
// Every backend renders one the same way: the author's own tokens pass
// through the literal-text hook, and the byte-ranged spans are substituted, each with
// a backend-specific escape (LaTeX's `\exposid{...}` in prose and
// `@\exposidnc{...}@` in code, the mpark backend's own convention, ...). That
// walk is written once here rather than once per backend. `escape_span` owns
// the macro spelling; `escape_text` defaults to identity for literal listings.
#ifndef BEMAN_SPECGEN_BACKEND_CODE_SPANS_HPP
#define BEMAN_SPECGEN_BACKEND_CODE_SPANS_HPP

#include <beman/specgen/ir.hpp>

#include <cstddef>
#include <functional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beman::specgen::backend {

namespace code_spans_detail {

// The slice [from, to) of `code.text`, with every span inside it substituted
// through `escape_span`; `next` is the first span not yet consumed and is
// advanced past the slice. A span that is malformed, overlaps, or straddles
// the slice's end is skipped and its text takes the literal-text hook -- the same
// defence the single-table walk always had, now also protecting a change run
// from a span the diff should never have let it cut.
template <typename EscapeSpan, typename EscapeText>
std::string render_slice(const ir::CodeText&                    code,
                         std::size_t                            from,
                         std::size_t                            to,
                         std::vector<ir::Span>::const_iterator& next,
                         EscapeSpan&&                           escape_span,
                         EscapeText&&                           escape_text) {
    std::string out;
    std::size_t pos = from;
    // substrate generic algorithm: a position-tracking left-to-right walk
    // over a sorted, non-overlapping span table is the array-substitution
    // primitive itself, not a fold in disguise -- there is no smaller unit
    // to fold over.
    for (; next != code.spans.end() && next->begin < to; ++next) {
        const ir::Span& span = *next;
        if (span.begin > code.text.size() || span.end > code.text.size() || span.begin < pos ||
            span.end < span.begin || span.end > to)
            continue; // defensive: ignore a malformed, overlapping or straddling span
        out += escape_text(std::string_view(code.text).substr(pos, span.begin - pos));
        out += escape_span(span, std::string_view(code.text).substr(span.begin, span.end - span.begin));
        pos = span.end;
    }
    out += escape_text(std::string_view(code.text).substr(pos, to - pos));
    return out;
}

// A run's rendered interior closed in the backend's mark one line at a time:
// the draft's `@...@` escape is line-local, so a run that spans lines (which
// the diff never produces, but a hand-written IR can) is wrapped per line
// rather than broken. An empty line is left empty.
template <typename WrapRun>
std::string wrap_run_lines(std::string_view interior, ir::Change change, WrapRun&& wrap_run) {
    return interior | std::views::split('\n') | std::views::transform([&](auto&& line) {
               const std::string_view text(line.begin(), line.end());
               return text.empty() ? std::string{} : std::string(wrap_run(change, text));
           }) |
           std::views::join_with('\n') | std::ranges::to<std::string>();
}

} // namespace code_spans_detail

// Tree -> text for one CodeText, splicing each span's backend-supplied
// escape between slices of the surrounding literal text.
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
//
// The full walk renders the wording diff's change runs (`CodeText::changes`,
// docs/plans/wording-diff.md) as well. `escape_span` renders a span outside a
// run and `escape_span_in_run` one inside, because inside a run the backend
// has already opened its own escape (`@\added{` ... `}@`, `@[` ... `]{.add}@`)
// and a span's codeblock form would close it early; `wrap_run(change, text)`
// closes a run's rendered text in the backend's mark. Runs are sorted and
// non-overlapping like spans, and a run contains its spans whole.
// `escape_text` handles literal slices before span macros are inserted. Code
// blocks keep the identity default; inline code in LaTeX escapes text-mode
// special characters without corrupting macros or their source offsets.
template <typename EscapeSpan, typename EscapeSpanInRun, typename WrapRun, typename EscapeText = std::identity>
std::string render_code_spans(const ir::CodeText& code,
                              EscapeSpan&&        escape_span,
                              EscapeSpanInRun&&   escape_span_in_run,
                              WrapRun&&           wrap_run,
                              EscapeText&&        escape_text = {}) {
    std::string out;
    std::size_t pos  = 0;
    auto        next = code.spans.begin();
    // substrate generic algorithm: the same position-tracking walk one level
    // up, over the change-run table, each run bracketing a slice of the span
    // walk below it.
    for (const auto& run : code.changes) {
        if (run.begin > code.text.size() || run.end > code.text.size() || run.begin < pos || run.end < run.begin)
            continue; // defensive: ignore a malformed or overlapping run
        out += code_spans_detail::render_slice(code, pos, run.begin, next, escape_span, escape_text);
        out += code_spans_detail::wrap_run_lines(
            code_spans_detail::render_slice(code, run.begin, run.end, next, escape_span_in_run, escape_text),
            run.change,
            wrap_run);
        pos = run.end;
    }
    out += code_spans_detail::render_slice(code, pos, code.text.size(), next, escape_span, escape_text);
    return out;
}

// The spans-only walk: a change run's text renders through the same escape
// as everything else and takes no mark. For code the diff never marks -- a
// prose code inline, whose marks sit on the inline itself -- and for a test
// of the span walk alone; a backend's code blocks use the full walk above.
template <typename EscapeSpan, typename EscapeText = std::identity>
std::string render_code_spans(const ir::CodeText& code, EscapeSpan&& escape_span, EscapeText&& escape_text = {}) {
    return render_code_spans(
        code,
        escape_span,
        escape_span,
        [](ir::Change, std::string_view text) { return std::string(text); },
        escape_text);
}

} // namespace beman::specgen::backend

#endif // BEMAN_SPECGEN_BACKEND_CODE_SPANS_HPP
