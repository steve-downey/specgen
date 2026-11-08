// tests/beman/specgen/frontend/expos_spellings.test.cpp          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/expos_spellings.hpp>
#include <beman/specgen/frontend/expos_spellings.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <beman/specgen/ir.hpp>

#include <string>
#include <variant>

// Covered end to end by the build_document scenario tests in this
// directory; this file holds the header's own checks.
TEST_CASE("expos_spellings - the header compiles on its own") { SUCCEED(); }

TEST_CASE("expos_spellings - template uses in prose retain surrounding code") {
    namespace ir = beman::specgen::ir;
    ir::ItemDescr description;
    description.leading = {
        ir::BodyParagraph{{ir::CodeInline{{"accepts_<T> && accepts_more", {}}}, ir::CodeInline{{"\"accepts_\"", {}}}}},
        ir::Note{{ir::BodyParagraph{{ir::CodeInline{{"accepts_<U>", {}}}}}}},
        ir::AuthoredCode{{"accepts_<T>();", {}}}};
    beman::specgen::frontend::rewrite_prose_expos(description, {{"accepts_", "accepts-type"}});
    const auto& code = std::get<ir::CodeInline>(std::get<ir::BodyParagraph>(description.leading[0]).text[0]).code;
    CHECK(code.text == "accepts-type<T> && accepts_more");
    REQUIRE(code.spans.size() == 1);
    CHECK(code.spans.front().kind == ir::SpanKind::ExposId);
    CHECK(code.spans.front().payload == "accepts-type");
    CHECK(code.spans.front().begin == 0);
    CHECK(code.spans.front().end == std::string("accepts-type").size());
    CHECK(std::get<ir::CodeInline>(std::get<ir::BodyParagraph>(description.leading[0]).text[1]).code.text ==
          "\"accepts_\"");
    const auto& note = std::get<ir::Note>(description.leading[1]);
    CHECK(std::get<ir::CodeInline>(std::get<ir::BodyParagraph>(note.body[0]).text[0]).code.text == "accepts-type<U>");
    CHECK(std::get<ir::AuthoredCode>(description.leading[2]).code.text == "accepts_<T>();");
}
