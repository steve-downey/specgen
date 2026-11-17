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

// What a backend that does not write the draft's line-break hints does with
// one: nothing. The hints are draft LaTeX typesetting (`CodeText::hints`), and
// only a backend writing draft LaTeX passes a writer of its own.
struct no_hints {
    std::string operator()(const ir::LayoutHint&, std::string_view, bool) const { return {}; }
};

using hint_cursor = std::vector<ir::LayoutHint>::const_iterator;

// The literal text [from, to) of `code.text` through the literal-text hook,
// with each hint inside it written where it sits. The text is escaped in the
// pieces between hints, so a hint's LaTeX is never escaped as code. `closed`
// also takes the hints at `to` itself: before a span, and at the end of the
// text, a hint there belongs to this piece; at the edge of a change run it
// belongs to the piece that follows. `write_hint` gets the hint, the text that
// follows it in this piece (which a LaTeX writer needs to know whether a
// control word runs into a letter), and whether the hint is inside a span's
// payload, which here it is not.
template <typename EscapeText, typename WriteHint>
std::string render_literal(const ir::CodeText& code,
                           std::size_t         from,
                           std::size_t         to,
                           bool                closed,
                           hint_cursor&        hint,
                           EscapeText&&        escape_text,
                           WriteHint&&         write_hint) {
    const std::string_view text = code.text;
    std::string            out;
    std::size_t            pos = from;
    // substrate generic algorithm: the position-tracking walk again, over the
    // hint table inside one literal piece.
    for (; hint != code.hints.end() && (hint->at < to || (closed && hint->at == to)); ++hint) {
        if (hint->at < pos)
            continue; // defensive: out of order, or left behind by a span that could not hold it
        out += escape_text(text.substr(pos, hint->at - pos));
        const std::string written = write_hint(*hint, text.substr(hint->at, to - hint->at), false);
        out += written;
        // A tie that was written stands in place of the space it is on.
        pos = hint->at + (!written.empty() && hint->at < to && ir::tied_space(*hint, text) ? 1 : 0);
    }
    out += escape_text(text.substr(pos, to - pos));
    return out;
}

// `span` with the hints strictly inside it written into its payload, which is
// how `\exposid{bound-en\-tity}` comes back out. Only a span whose payload is
// the text it covers can hold one, since the hint's offset is into the text;
// that is every span the draft importer makes a hint inside. Any other span's
// hints are passed over.
template <typename WriteHint>
ir::Span span_with_hints(const ir::CodeText& code, const ir::Span& span, hint_cursor& hint, WriteHint&& write_hint) {
    const bool  holds = span.payload == std::string_view(code.text).substr(span.begin, span.end - span.begin);
    ir::Span    woven = span;
    std::size_t pos   = 0;
    if (holds)
        woven.payload.clear();
    // substrate generic algorithm: the same walk, over the hints inside one span.
    for (; hint != code.hints.end() && hint->at < span.end; ++hint) {
        if (!holds || hint->at <= span.begin)
            continue;
        const std::size_t at = hint->at - span.begin;
        woven.payload += std::string_view(span.payload).substr(pos, at - pos);
        const std::string written = write_hint(*hint, std::string_view(span.payload).substr(at), true);
        woven.payload += written;
        pos = at + (!written.empty() && ir::tied_space(*hint, code.text) ? 1 : 0);
    }
    if (holds)
        woven.payload += std::string_view(span.payload).substr(pos);
    return woven;
}

// The slice [from, to) of `code.text`, with every span inside it substituted
// through `escape_span`; `next` is the first span not yet consumed and is
// advanced past the slice, and `hint` the first hint, likewise. A span that is
// malformed, overlaps, or straddles
// the slice's end is skipped and its text takes the literal-text hook -- the same
// defence the single-table walk always had, now also protecting a change run
// from a span the diff should never have let it cut.
template <typename EscapeSpan, typename EscapeText, typename WriteHint>
std::string render_slice(const ir::CodeText&                    code,
                         std::size_t                            from,
                         std::size_t                            to,
                         std::vector<ir::Span>::const_iterator& next,
                         hint_cursor&                           hint,
                         EscapeSpan&&                           escape_span,
                         EscapeText&&                           escape_text,
                         WriteHint&&                            write_hint) {
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
        out += render_literal(code, pos, span.begin, true, hint, escape_text, write_hint);
        const std::string_view spelling = std::string_view(code.text).substr(span.begin, span.end - span.begin);
        const bool             inside   = hint != code.hints.end() && hint->at < span.end;
        out += inside ? escape_span(span_with_hints(code, span, hint, write_hint), spelling)
                      : escape_span(span, spelling);
        pos = span.end;
    }
    out += render_literal(code, pos, to, to == code.text.size(), hint, escape_text, write_hint);
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
//
// `write_hint` writes one of the draft's line-break hints (`CodeText::hints`)
// where it sits, in literal text or inside a span's payload:
// `(const ir::LayoutHint&, std::string_view following, bool in_span) ->
// std::string`. The default writes nothing, which is right for every backend
// but the ones writing draft LaTeX.
template <typename EscapeSpan,
          typename EscapeSpanInRun,
          typename WrapRun,
          typename EscapeText = std::identity,
          typename WriteHint  = code_spans_detail::no_hints>
std::string render_code_spans(const ir::CodeText& code,
                              EscapeSpan&&        escape_span,
                              EscapeSpanInRun&&   escape_span_in_run,
                              WrapRun&&           wrap_run,
                              EscapeText&&        escape_text = {},
                              WriteHint&&         write_hint  = {}) {
    std::string out;
    std::size_t pos  = 0;
    auto        next = code.spans.begin();
    auto        hint = code.hints.begin();
    // substrate generic algorithm: the same position-tracking walk one level
    // up, over the change-run table, each run bracketing a slice of the span
    // walk below it.
    for (const auto& run : code.changes) {
        if (run.begin > code.text.size() || run.end > code.text.size() || run.begin < pos || run.end < run.begin)
            continue; // defensive: ignore a malformed or overlapping run
        out += code_spans_detail::render_slice(code, pos, run.begin, next, hint, escape_span, escape_text, write_hint);
        out += code_spans_detail::wrap_run_lines(
            code_spans_detail::render_slice(
                code, run.begin, run.end, next, hint, escape_span_in_run, escape_text, write_hint),
            run.change,
            wrap_run);
        pos = run.end;
    }
    out +=
        code_spans_detail::render_slice(code, pos, code.text.size(), next, hint, escape_span, escape_text, write_hint);
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
