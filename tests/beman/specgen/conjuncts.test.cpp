// tests/beman/specgen/conjuncts.test.cpp                           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/conjuncts.hpp>
#include <beman/specgen/conjuncts.hpp> // Re-inclusion verification

#include <beman/specgen/foundation/overloaded.hpp>

#include <beman/specgen/body_test.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <variant>
#include <vector>

using namespace beman::specgen::conjuncts;
using namespace beman::specgen::ir;
using beman::specgen::foundation::overloaded;

namespace {

// The shape the phrasing rewriter will produce: "`X` is `true`".
Paragraph is_true(std::string trait) {
    return {CodeInline{{std::move(trait), {}}}, TextInline{" is "}, CodeInline{{"true", {}}}};
}

std::vector<Paragraph> traits(std::size_t n) {
    std::vector<Paragraph> out;
    for (std::size_t i = 0; i < n; ++i)
        out.push_back(is_true("T" + std::to_string(i)));
    return out;
}

// A paragraph read back as text, code in backquotes, so a case can state the
// conjunction it expects as the sentence it is. How a backend lays the
// paragraph out is that backend's test's business.
std::string plain_prose(const PlainInline& piece) {
    return std::visit(overloaded{
                          [](const TextInline& v) { return v.text; },
                          [](const CodeInline& v) { return '`' + v.code.text + '`'; },
                          [](const RefInline& v) { return "[" + v.stable_name + "]"; },
                          [](const ConceptRef& v) { return v.name; },
                      },
                      piece);
}

std::string prose(const Paragraph& paragraph) {
    std::string out;
    for (const Inline& piece : paragraph)
        out += std::visit(overloaded{
                              [](const TextInline& v) { return plain_prose(v); },
                              [](const CodeInline& v) { return plain_prose(v); },
                              [](const RefInline& v) { return plain_prose(v); },
                              [](const ConceptRef& v) { return plain_prose(v); },
                              // A changed run (the wording diff's mark) flattens to its contents;
                              // no conjunct carries one, so the mark itself is not shown.
                              [](const ChangedInline& v) {
                                  std::string run;
                                  for (const PlainInline& inner : v.inlines)
                                      run += plain_prose(inner);
                                  return run;
                              },
                          },
                          piece);
    return out;
}

DescriptionElement conjunction(std::size_t n, Options options = {}) {
    DescriptionElement element;
    element.kind = ElementKind::Mandates;
    render_into(traits(n), element, options);
    return element;
}

} // namespace

TEST_CASE("conjuncts - HeaderIsIdempotent") {
    // Placeholder: verifies header re-inclusion safety and build coherency.
    // This test always passes if the file compiles.
    REQUIRE(true);
}

TEST_CASE("conjuncts - sentence form for one, two, and three") {
    const DescriptionElement one = conjunction(1);
    REQUIRE(body_test::paragraph_count(one) == 1);
    CHECK(prose(body_test::paragraph(one, 0)) == "`T0` is `true`.");
    CHECK_FALSE((body_test::first<beman::specgen::ir::Itemize>(one) != nullptr));

    const DescriptionElement two = conjunction(2);
    REQUIRE(body_test::paragraph_count(two) == 1);
    CHECK(prose(body_test::paragraph(two, 0)) == "`T0` is `true` and `T1` is `true`.");

    // Serial comma before the final conjunct, as the draft writes it.
    const DescriptionElement three = conjunction(3);
    REQUIRE(body_test::paragraph_count(three) == 1);
    CHECK(prose(body_test::paragraph(three, 0)) == "`T0` is `true`, `T1` is `true`, and `T2` is `true`.");
}

TEST_CASE("conjuncts - beyond the threshold becomes an itemize") {
    const DescriptionElement four = conjunction(4);
    CHECK((body_test::paragraph_count(four) == 0));
    REQUIRE((body_test::first<beman::specgen::ir::Itemize>(four) != nullptr));
    REQUIRE(body_test::first<beman::specgen::ir::Itemize>(four)->items.size() == 4);
    CHECK(prose(body_test::first<beman::specgen::ir::Itemize>(four)->items[0]) == "`T0` is `true`,");
    CHECK(prose(body_test::first<beman::specgen::ir::Itemize>(four)->items[1]) == "`T1` is `true`,");
    CHECK(prose(body_test::first<beman::specgen::ir::Itemize>(four)->items[2]) == "`T2` is `true`,");
    CHECK(prose(body_test::first<beman::specgen::ir::Itemize>(four)->items[3]) == "`T3` is `true`.");
}

TEST_CASE("conjuncts - the threshold is configurable") {
    Options options;
    options.sentence_threshold = 2;
    CHECK((body_test::first<beman::specgen::ir::Itemize>(conjunction(3, options)) != nullptr));
    CHECK_FALSE((body_test::first<beman::specgen::ir::Itemize>(conjunction(2, options)) != nullptr));

    options.sentence_threshold    = 5;
    const DescriptionElement four = conjunction(4, options);
    CHECK_FALSE((body_test::first<beman::specgen::ir::Itemize>(four) != nullptr));
    REQUIRE(body_test::paragraph_count(four) == 1);
    CHECK(prose(body_test::paragraph(four, 0)) ==
          "`T0` is `true`, `T1` is `true`, `T2` is `true`, and `T3` is `true`.");
}

TEST_CASE("conjuncts - an empty conjunction contributes nothing") {
    DescriptionElement element;
    element.kind = ElementKind::Constraints;
    render_into({}, element);
    CHECK((body_test::paragraph_count(element) == 0));
    CHECK(!(body_test::first<beman::specgen::ir::Itemize>(element) != nullptr));
}

TEST_CASE("conjuncts - join_sentence and as_itemize directly") {
    CHECK(join_sentence({}).empty());

    const auto one = join_sentence(traits(1));
    REQUIRE(one.size() == 4); // code, " is ", code, "."
    CHECK(std::get<TextInline>(one[3]).text == ".");

    const auto list = as_itemize(traits(3));
    REQUIRE(list.items.size() == 3);
    CHECK(std::get<TextInline>(list.items[0].back()).text == ",");
    CHECK(std::get<TextInline>(list.items[1].back()).text == ",");
    CHECK(std::get<TextInline>(list.items[2].back()).text == ".");
}

TEST_CASE("conjuncts - a lead-in paragraph is kept ahead of the list") {
    DescriptionElement element;
    element.kind = ElementKind::Constraints;
    element.body.push_back(beman::specgen::ir::BodyParagraph{
        beman::specgen::ir::Paragraph{TextInline{"All of the following are true:"}}});
    Options options;
    options.sentence_threshold = 0; // force the list form
    render_into(traits(2), element, options);

    REQUIRE(body_test::paragraph_count(element) == 1);
    CHECK(prose(body_test::paragraph(element, 0)) == "All of the following are true:");
    REQUIRE((body_test::first<beman::specgen::ir::Itemize>(element) != nullptr));
    REQUIRE(body_test::first<beman::specgen::ir::Itemize>(element)->items.size() == 2);
    CHECK(prose(body_test::first<beman::specgen::ir::Itemize>(element)->items[1]) == "`T1` is `true`.");
}
