// tests/beman/specgen/backend/draft_spans.test.cpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/backend/draft_spans.hpp>
#include <beman/specgen/backend/draft_spans.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

using namespace beman::specgen::ir;
namespace backend = beman::specgen::backend;

TEST_CASE("draft_spans - draft_span_prose writes each span as the draft's macro") {
    CHECK(backend::draft_span_prose({0, 3, SpanKind::ExposId, "val"}, "VAL") == "\\exposid{val}");
    CHECK(backend::draft_span_prose({0, 1, SpanKind::SeeBelow, ""}, "x") == "\\seebelow");
    CHECK(backend::draft_span_prose({0, 1, SpanKind::ImplDefined, ""}, "x") == "\\impdef");
    CHECK(backend::draft_span_prose({0, 1, SpanKind::Placeholder, "T"}, "x") == "\\placeholder{T}");
    CHECK(backend::draft_span_prose({0, 1, SpanKind::Ref, "optional.ctor"}, "x") == "\\ref{optional.ctor}");
    CHECK(backend::draft_span_prose({0, 8, SpanKind::LibraryIndex, ""}, "optional") == "\\libglobal{optional}");
    CHECK(backend::draft_span_prose({0, 5, SpanKind::LibraryIndex, "optional"}, "reset") ==
          "\\libmember{reset}{optional}");
}

// Inside a codeblock the draft escapes back to LaTeX with @...@, except for a
// Ref, which only ever sits in a comment that is already in TeX mode, and an
// exposition-only name takes the no-correction form.
TEST_CASE("draft_spans - draft_span_codeblock wraps every span but a Ref in the draft's escape") {
    CHECK(backend::draft_span_codeblock({0, 3, SpanKind::ExposId, "val"}, "VAL") == "@\\exposidnc{val}@");
    CHECK(backend::draft_span_codeblock({0, 1, SpanKind::SeeBelow, ""}, "x") == "@\\seebelow@");
    CHECK(backend::draft_span_codeblock({0, 1, SpanKind::Ref, "optional.ctor"}, "x") == "\\ref{optional.ctor}");
}

TEST_CASE("draft_spans - draft_code_inline wraps a partial span in tcode but not a whole one") {
    CHECK(backend::draft_code_inline(CodeInline{{"val", {{0, 3, SpanKind::ExposId, "val"}}}}) == "\\exposid{val}");
    CHECK(backend::draft_code_inline(CodeInline{{"x.val", {{2, 5, SpanKind::ExposId, "val"}}}}) ==
          "\\tcode{x.\\exposid{val}}");
}
