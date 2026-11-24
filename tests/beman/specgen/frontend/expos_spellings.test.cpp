// tests/beman/specgen/frontend/expos_spellings.test.cpp          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/expos_spellings.hpp>
#include <beman/specgen/frontend/expos_spellings.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <beman/specgen/ir.hpp>

#include <set>
#include <string>
#include <utility>
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

TEST_CASE("expos_spellings - every use in a code span is renamed, by either spelling") {
    namespace ir         = beman::specgen::ir;
    const auto rewritten = [](std::string text, const std::set<std::string>& taken = {}) {
        ir::ItemDescr description;
        description.leading = {ir::BodyParagraph{{ir::CodeInline{{std::move(text), {}}}}}};
        beman::specgen::frontend::rewrite_prose_expos(
            description, {{"value_", "value"}, {"p_", "ptr"}, {"has_next_", "has-next"}}, taken);
        return std::get<ir::CodeInline>(std::get<ir::BodyParagraph>(description.leading[0]).text[0]).code;
    };
    const auto covered = [](const ir::CodeText& code) {
        std::string out;
        // substrate generic algorithm: a test-only join of each span's text.
        for (const ir::Span& span : code.spans)
            out += code.text.substr(span.begin, span.end - span.begin) + "|";
        return out;
    };

    // The header's spelling, wherever it is in the span.
    const ir::CodeText both = rewritten("*p_ == value_");
    CHECK(both.text == "*ptr == value");
    CHECK(covered(both) == "ptr|value|");

    // The paper's spelling, kebab-case included.
    const ir::CodeText paper = rewritten("has-next && rhs.value");
    CHECK(paper.text == "has-next && rhs.value");
    CHECK(covered(paper) == "has-next|value|");

    // Whole identifiers and whole kebab names only.
    CHECK(rewritten("evaluate(values, not-has-next)").spans.empty());

    // A name the wording uses for something else is not an exposition name,
    // while the written spelling still is.
    const ir::CodeText parameter = rewritten("value_ = value", {"value"});
    CHECK(parameter.text == "value = value");
    CHECK(covered(parameter) == "value|");
    CHECK(parameter.spans.front().begin == 0);

    // A literal's contents are text.
    CHECK(rewritten("\"value_\" 'p_'").spans.empty());

    // A name already in the mpark spelling is the exposition name, taken or
    // not, and is not wrapped twice (issue #432).
    const ir::CodeText dollared = rewritten("$value$ + $has-next$ + $p_$", {"value"});
    CHECK(dollared.text == "value + has-next + ptr");
    CHECK(covered(dollared) == "value|has-next|ptr|");

    // A name reached through a scope is that scope's (issue #432).
    const ir::CodeText scoped = rewritten("ns::value<int> + T::value_ + value_");
    CHECK(scoped.text == "ns::value<int> + T::value_ + value");
    CHECK(covered(scoped) == "value|");

    // An exposition name followed by a template argument list is a
    // template's, while the written spelling still names the member.
    const ir::CodeText templated = rewritten("value<int> + value < 1 + p_<int>");
    CHECK(templated.text == "value<int> + value < 1 + ptr<int>");
    CHECK(covered(templated) == "value|ptr|");
}
