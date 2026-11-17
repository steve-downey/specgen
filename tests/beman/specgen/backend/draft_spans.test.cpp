// tests/beman/specgen/backend/draft_spans.test.cpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/backend/draft_spans.hpp>
#include <beman/specgen/backend/draft_spans.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>
#include <string_view>

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
    CHECK(backend::draft_code_inline(CodeInline{{"is_constructible_v<VT, F>", {}}}) ==
          R"(\tcode{is_constructible_v<VT, F>})");
    CHECK(backend::draft_code_inline(CodeInline{{R"(& && # % _ { } ~ ^ $ \)", {}}}) ==
          R"(\tcode{\& \&\& \# \% _ \{ \} \textasciitilde{} \textasciicircum{} \$ \textbackslash{}})");
    CHECK(backend::draft_code_inline(CodeInline{{"&VAL_", {{1, 4, SpanKind::ExposId, "value-name"}}}}) ==
          R"(\tcode{\&\exposid{value-name}_})");
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

// --- line-break hints in prose (issue #314) --------------------------------

TEST_CASE("draft_spans - draft_hinted_text writes each hint where it sits and escapes only the text") {
    const auto upper = [](std::string_view text) {
        std::string out(text);
        for (char& c : out)
            c = c == 'a' ? 'A' : c;
        return out;
    };
    CHECK(backend::draft_hinted_text("plain", {}) == "plain");
    CHECK(backend::draft_hinted_text("UnaryTypeTrait", {{5, LayoutHintKind::Hyphen}, {9, LayoutHintKind::Hyphen}}) ==
          "Unary\\-Type\\-Trait");
    // The text between hints goes through the hook; the hint does not.
    CHECK(backend::draft_hinted_text("banana", {{3, LayoutHintKind::Break, true}}, upper) == "bAn\\brk{}AnA");
    // A control word with no group takes a space before a letter and at the
    // end of the text, where another inline's macro follows; not before
    // punctuation, and never after `\-`.
    CHECK(backend::draft_hinted_text("ab, ", {{1, LayoutHintKind::Break}, {2, LayoutHintKind::Break}}) ==
          "a\\brk b\\brk, ");
    CHECK(backend::draft_hinted_text("a, ", {{3, LayoutHintKind::LineBreak}}) == "a, \\linebreak ");
    CHECK(backend::draft_hinted_text("a", {{1, LayoutHintKind::Hyphen}}) == "a\\-");
    CHECK(backend::draft_hinted_text("", {{0, LayoutHintKind::NewLine, true}}) == "\\newline{}");
    // A hint out of order or past the end is passed over.
    CHECK(backend::draft_hinted_text("ab", {{9, LayoutHintKind::Break}}) == "ab");
}

TEST_CASE("draft_spans - a prose inline's name takes its hints inside the macro") {
    CHECK(backend::draft_old_concept({"UnaryTypeTrait", {{5, LayoutHintKind::Hyphen}}}) ==
          "\\oldconcept{Unary\\-TypeTrait}");
    CHECK(backend::draft_concept({.name = "invocable", .hints = {{2, LayoutHintKind::Hyphen}}}) ==
          "\\libconcept{in\\-vocable}");
    CHECK(backend::draft_placeholder(
              {.text = "cv2", .cv_qualifier = true, .hints = {{2, LayoutHintKind::Break, true}}}) ==
          "\\cvqual{cv\\brk{}2}");
}

// --- the tie (issue #312) ---------------------------------------------------

TEST_CASE("draft_spans - a tie is written as ~ in place of the space it is on") {
    CHECK(backend::draft_hinted_text("Within subclause ", {{16, LayoutHintKind::Tie}}) == "Within subclause~");
    CHECK(backend::draft_hinted_text(" is in Table 4", {{0, LayoutHintKind::Tie}, {12, LayoutHintKind::Tie}}) ==
          "~is in Table~4");
    // A tie at the end of the text takes no space after it, as a control word would.
    CHECK(backend::draft_hinted_text("a ", {{1, LayoutHintKind::Tie}}) == "a~");
    // In code the same: the space before the optional part is the tie.
    const CodeInline code{
        {"R(*)(G, A...) noexcept", {{14, 22, SpanKind::Optional, ""}}, {}, {{13, LayoutHintKind::Tie}}}};
    CHECK(backend::draft_code_inline(code) == "\\tcode{R(*)(G, A...)~\\opt{noexcept}}");
    // In a listing the text is verbatim, where `~` is a tilde: the space stays.
    CHECK(backend::render_code_spans(code.code,
                                     backend::draft_span_codeblock,
                                     backend::draft_span_escaped,
                                     backend::draft_change_codeblock,
                                     std::identity{},
                                     backend::draft_layout_hint_listing) == "R(*)(G, A...) @\\opt{noexcept}@");
}
