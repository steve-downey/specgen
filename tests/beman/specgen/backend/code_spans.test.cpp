// tests/beman/specgen/backend/code_spans.test.cpp                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/backend/code_spans.hpp>
#include <beman/specgen/backend/code_spans.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>
#include <string_view>

using namespace beman::specgen::ir;
namespace backend = beman::specgen::backend;

namespace {
// A stand-in backend escape: wraps a span's payload (or kind name) in
// brackets, distinct from any real backend's spelling so a test failure
// clearly points at the substrate rather than a backend's own escape table.
std::string bracket_escape(const Span& span, std::string_view spelling) {
    switch (span.kind) {
    case SpanKind::ExposId:
        return "[expos:" + span.payload + ']';
    case SpanKind::SeeBelow:
        return "[seebelow]";
    case SpanKind::ImplDefined:
        return "[impl-defined]";
    case SpanKind::Placeholder:
        return "[placeholder:" + span.payload + ']';
    case SpanKind::Ref:
        return "[ref:" + span.payload + ']';
    case SpanKind::LibraryIndex:
        return "[index:" + std::string(spelling) + ':' + span.payload + ']';
    case SpanKind::CvQualifier:
        return "[cvqual:" + span.payload + ']';
    case SpanKind::Optional:
        return "[opt:" + std::string(spelling) + ']';
    }
    return {};
}
} // namespace

TEST_CASE("code_spans - render_code_spans with no spans returns the text verbatim") {
    const CodeText code{"int x;", {}};
    CHECK(backend::render_code_spans(code, bracket_escape) == "int x;");
}

TEST_CASE("code_spans - render_code_spans splices one span's escape into the surrounding text") {
    const CodeText code{"T VAL;", {{2, 5, SpanKind::ExposId, "val"}}};
    CHECK(backend::render_code_spans(code, bracket_escape) == "T [expos:val];");
}

TEST_CASE("code_spans - render_code_spans handles multiple non-overlapping spans in order") {
    const CodeText code{
        "a b c optional",
        {
            {0, 1, SpanKind::Placeholder, "p1"},
            {4, 5, SpanKind::Ref, "p2"},
            {6, 14, SpanKind::LibraryIndex, ""},
        },
    };
    CHECK(backend::render_code_spans(code, bracket_escape) == "[placeholder:p1] b [ref:p2] [index:optional:]");
}

TEST_CASE("code_spans - render_code_spans defensively skips a malformed span") {
    // begin > text.size(): out of range, ignored per the substrate's
    // defensive check -- the raw text still comes through untouched.
    const CodeText code{"abc", {{10, 12, SpanKind::SeeBelow, ""}}};
    CHECK(backend::render_code_spans(code, bracket_escape) == "abc");
}

TEST_CASE("code_spans - literal escaping preserves span offsets and inserted macros") {
    const CodeText code{"&VAL&", {{1, 4, SpanKind::ExposId, "value"}}};
    const auto     escape_text = [](std::string_view text) {
        return text.empty() ? std::string{} : "literal(" + std::string(text) + ')';
    };
    CHECK(backend::render_code_spans(code, bracket_escape, escape_text) == "literal(&)[expos:value]literal(&)");
    CHECK(backend::render_code_spans(CodeText{"&", {{0, 8, SpanKind::ExposId, "bad"}}}, bracket_escape, escape_text) ==
          "literal(&)");
}

// --- the wording diff's change runs (docs/plans/wording-diff.md) -----------

namespace {
std::string plain_escape(const Span& span, std::string_view) { return "<" + span.payload + ">"; }
std::string wrap_change(Change change, std::string_view text) {
    return (change == Change::Added ? "+(" : "-(") + std::string(text) + ")";
}
} // namespace

TEST_CASE("code_spans - a change run is wrapped, and its spans take the in-run escape") {
    const CodeText code{
        "T VAL; U w;", {{2, 5, SpanKind::ExposId, "val"}}, {{0, 6, Change::Added}, {7, 11, Change::Removed}}};
    CHECK(backend::render_code_spans(code, bracket_escape, plain_escape, wrap_change) == "+(T <val>;) -(U w;)");
}

TEST_CASE("code_spans - a run spanning lines is wrapped one line at a time, empty lines left empty") {
    const CodeText code{"a\n\nb", {}, {{0, 4, Change::Added}}};
    CHECK(backend::render_code_spans(code, bracket_escape, plain_escape, wrap_change) == "+(a)\n\n+(b)");
}

TEST_CASE("code_spans - the spans-only walk leaves a run's text unmarked") {
    const CodeText code{"T VAL;", {{2, 5, SpanKind::ExposId, "val"}}, {{0, 6, Change::Added}}};
    CHECK(backend::render_code_spans(code, bracket_escape) == "T [expos:val];");
}

// --- the draft's line-break hints (CodeText::hints) --------------------------

namespace {
// A stand-in hint writer that shows everything it was handed: the kind, the
// empty group, the first character of what follows, and whether the hint is
// inside a span (braces) or in the text (parentheses).
std::string show_hint(const LayoutHint& hint, std::string_view following, bool in_span) {
    return std::string(in_span ? "{" : "(") + std::string(layout_hint_name(hint.kind)) + (hint.braced ? "!" : "") +
           '|' + std::string(following.substr(0, 1)) + (in_span ? "}" : ")");
}
} // namespace

TEST_CASE("code_spans - a hint is written where it sits: in the text, at a span's edges, and inside a span") {
    // A hint at a span's first byte is before the span, one at its end is after
    // it, and only one strictly inside goes into its payload.
    const CodeText code{"ab VALUE cd",
                        {{3, 8, SpanKind::ExposId, "VALUE"}},
                        {},
                        {{1, LayoutHintKind::Hyphen},
                         {3, LayoutHintKind::Break, true},
                         {5, LayoutHintKind::Hyphen},
                         {8, LayoutHintKind::NewLine},
                         {11, LayoutHintKind::LineBreak}}};
    CHECK(backend::render_code_spans(code, bracket_escape, plain_escape, wrap_change, std::identity{}, show_hint) ==
          "a(hyphen|b)b (brk!|)[expos:VA{hyphen|L}LUE](newline| ) cd(linebreak|)");
    // A backend that passes no writer writes none of them.
    CHECK(backend::render_code_spans(code, bracket_escape) == "ab [expos:VALUE] cd");
}

TEST_CASE("code_spans - a hint inside a span whose payload is not its text is passed over") {
    // The offset is into the text, so it says nothing about where in `val` to go.
    const CodeText code{"T VALUE;", {{2, 7, SpanKind::ExposId, "val"}}, {}, {{4, LayoutHintKind::Hyphen}}};
    CHECK(backend::render_code_spans(code, bracket_escape, plain_escape, wrap_change, std::identity{}, show_hint) ==
          "T [expos:val];");
}

TEST_CASE("code_spans - a hint at the edge of a change run is written once, inside the run") {
    const CodeText code{"abcd", {}, {{2, 4, Change::Added}}, {{2, LayoutHintKind::Break}}};
    CHECK(backend::render_code_spans(code, bracket_escape, plain_escape, wrap_change, std::identity{}, show_hint) ==
          "ab+((brk|c)cd)");
}

TEST_CASE("code_spans - a hint between the pieces of escaped text is not itself escaped") {
    const auto     escape_text = [](std::string_view text) { return "<" + std::string(text) + ">"; };
    const CodeText code{"a&b", {}, {}, {{2, LayoutHintKind::Break, true}}};
    CHECK(backend::render_code_spans(code, bracket_escape, plain_escape, wrap_change, escape_text, show_hint) ==
          "<a&>(brk!|b)<b>");
}

TEST_CASE("code_spans - a malformed run is skipped and a span straddling a run's end passes through raw") {
    const CodeText code{"abcdef", {{2, 5, SpanKind::ExposId, "x"}}, {{0, 3, Change::Added}, {9, 12, Change::Added}}};
    CHECK(backend::render_code_spans(code, bracket_escape, plain_escape, wrap_change) == "+(abc)def");
}
