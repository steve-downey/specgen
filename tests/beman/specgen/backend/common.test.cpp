// tests/beman/specgen/backend/common.test.cpp                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/backend/common.hpp>
#include <beman/specgen/backend/common.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace beman::specgen::ir;
namespace common = beman::specgen::backend::common;

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

TEST_CASE("backend common - HeaderIsIdempotent") {
    // Placeholder: verifies header re-inclusion safety and build coherency.
    REQUIRE(true);
}

TEST_CASE("backend common - render_code_spans with no spans returns the text verbatim") {
    const CodeText code{"int x;", {}};
    CHECK(common::render_code_spans(code, bracket_escape) == "int x;");
}

TEST_CASE("backend common - render_code_spans splices one span's escape into the surrounding text") {
    const CodeText code{"T VAL;", {{2, 5, SpanKind::ExposId, "val"}}};
    CHECK(common::render_code_spans(code, bracket_escape) == "T [expos:val];");
}

TEST_CASE("backend common - render_code_spans handles multiple non-overlapping spans in order") {
    const CodeText code{
        "a b c optional",
        {
            {0, 1, SpanKind::Placeholder, "p1"},
            {4, 5, SpanKind::Ref, "p2"},
            {6, 14, SpanKind::LibraryIndex, ""},
        },
    };
    CHECK(common::render_code_spans(code, bracket_escape) == "[placeholder:p1] b [ref:p2] [index:optional:]");
}

TEST_CASE("backend common - render_code_spans defensively skips a malformed span") {
    // begin > text.size(): out of range, ignored per the substrate's
    // defensive check -- the raw text still comes through untouched.
    const CodeText code{"abc", {{10, 12, SpanKind::SeeBelow, ""}}};
    CHECK(common::render_code_spans(code, bracket_escape) == "abc");
}
