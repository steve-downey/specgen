// tests/beman/specgen/backend/code_spans.test.cpp                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/backend/code_spans.hpp>
#include <beman/specgen/backend/code_spans.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("code_spans - a malformed run is skipped and a span straddling a run's end passes through raw") {
    const CodeText code{"abcdef", {{2, 5, SpanKind::ExposId, "x"}}, {{0, 3, Change::Added}, {9, 12, Change::Added}}};
    CHECK(backend::render_code_spans(code, bracket_escape, plain_escape, wrap_change) == "+(abc)def");
}
