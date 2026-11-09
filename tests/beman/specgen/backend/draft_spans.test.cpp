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

TEST_CASE("draft_spans - inline code escapes literal text without escaping span macros") {
    CHECK(backend::draft_code_inline(CodeInline{{R"(& && # % _ { } ~ ^ $ \)", {}}}) ==
          R"(\tcode{\& \&\& \# \% \_ \{ \} \textasciitilde{} \textasciicircum{} \$ \textbackslash{}})");
    CHECK(backend::draft_code_inline(CodeInline{{"&VAL_", {{1, 4, SpanKind::ExposId, "value-name"}}}}) ==
          R"(\tcode{\&\exposid{value-name}\_})");
    CHECK(backend::draft_code_inline(CodeInline{{"VAL", {{0, 3, SpanKind::ExposId, "value-name"}}}}) ==
          R"(\exposid{value-name})");
}

// --- the draft's change marks (docs/plans/wording-diff.md) -----------------

TEST_CASE("draft_spans - the escaped form is what the codeblock form wraps") {
    const Span expos{0, 3, SpanKind::ExposId, "val"};
    CHECK(backend::draft_span_escaped(expos, "VAL") == "\\exposidnc{val}");
    CHECK(backend::draft_span_codeblock(expos, "VAL") == "@\\exposidnc{val}@");
    const Span ref{0, 3, SpanKind::Ref, "a.b"};
    CHECK(backend::draft_span_escaped(ref, "a.b") == "\\ref{a.b}");
    CHECK(backend::draft_span_codeblock(ref, "a.b") == "\\ref{a.b}");
}

TEST_CASE("draft_spans - the change marks are macros.tex's own") {
    CHECK(backend::draft_change_prose(Change::Same, "x") == "x");
    CHECK(backend::draft_change_prose(Change::Added, "x") == "\\added{x}");
    CHECK(backend::draft_change_prose(Change::Removed, "x") == "\\removed{x}");
    CHECK(backend::draft_change_codeblock(Change::Same, "x") == "x");
    CHECK(backend::draft_change_codeblock(Change::Added, "x") == "@\\added{x}@");
    CHECK(backend::draft_change_codeblock(Change::Removed, "x") == "@\\removed{x}@");
    CHECK(!backend::draft_change_environment(Change::Same).has_value());
    CHECK(backend::draft_change_environment(Change::Added) == "addedblock");
    CHECK(backend::draft_change_environment(Change::Removed) == "removedblock");
}
