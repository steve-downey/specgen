// tests/beman/specgen/conjuncts.test.cpp                           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/conjuncts.hpp>
#include <beman/specgen/conjuncts.hpp> // Re-inclusion verification

#include <beman/specgen/backend/latex.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <variant>
#include <vector>

namespace latex = beman::specgen::backend::latex;
using namespace beman::specgen::conjuncts;
using namespace beman::specgen::ir;

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

// Render a conjunction through an element so the prose is readable as wording.
std::string wording(std::size_t n, Options options = {}) {
    DescriptionElement element;
    element.kind = ElementKind::Mandates;
    render_into(traits(n), element, options);

    SpecItem item;
    item.decl.signatures.push_back({"int f();", {}});
    item.descr.elements.push_back(std::move(element));
    return latex::render_to_string(item);
}

} // namespace

TEST_CASE("conjuncts - HeaderIsIdempotent") {
    // Placeholder: verifies header re-inclusion safety and build coherency.
    // This test always passes if the file compiles.
    REQUIRE(true);
}

TEST_CASE("conjuncts - sentence form for one, two, and three") {
    CHECK(wording(1).find("\\mandates\n\\tcode{T0} is \\tcode{true}.\n") != std::string::npos);
    CHECK(wording(2).find("\\tcode{T0} is \\tcode{true} and \\tcode{T1} is \\tcode{true}.\n") != std::string::npos);
    // Serial comma before the final conjunct, as the draft writes it.
    CHECK(wording(3).find("\\tcode{T0} is \\tcode{true}, \\tcode{T1} is \\tcode{true}, and "
                          "\\tcode{T2} is \\tcode{true}.\n") != std::string::npos);
}

TEST_CASE("conjuncts - beyond the threshold becomes an itemize") {
    const std::string out = wording(4);
    const std::string expected =
        R"(\begin{itemdescr}
\pnum
\mandates
\begin{itemize}
\item \tcode{T0} is \tcode{true},
\item \tcode{T1} is \tcode{true},
\item \tcode{T2} is \tcode{true},
\item \tcode{T3} is \tcode{true}.
\end{itemize}
\end{itemdescr}
)";
    INFO(out);
    CHECK(out.find(expected) != std::string::npos);
}

TEST_CASE("conjuncts - the threshold is configurable") {
    Options options;
    options.sentence_threshold = 2;
    CHECK(wording(3, options).find("\\begin{itemize}") != std::string::npos);
    CHECK(wording(2, options).find("\\begin{itemize}") == std::string::npos);

    options.sentence_threshold = 5;
    CHECK(wording(4, options).find("\\begin{itemize}") == std::string::npos);
    CHECK(wording(4, options).find(", and \\tcode{T3} is \\tcode{true}.") != std::string::npos);
}

TEST_CASE("conjuncts - an empty conjunction contributes nothing") {
    DescriptionElement element;
    element.kind = ElementKind::Constraints;
    render_into({}, element);
    CHECK(element.paragraphs.empty());
    CHECK(!element.itemize.has_value());
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

TEST_CASE("conjuncts - a lead-in paragraph keeps the list in its own pnum") {
    DescriptionElement element;
    element.kind = ElementKind::Constraints;
    element.paragraphs.push_back({TextInline{"All of the following are true:"}});
    Options options;
    options.sentence_threshold = 0; // force the list form
    render_into(traits(2), element, options);

    SpecItem item;
    item.decl.signatures.push_back({"int f();", {}});
    item.descr.elements.push_back(std::move(element));

    const std::string expected =
        R"(\pnum
\constraints
All of the following are true:
\begin{itemize}
\item \tcode{T0} is \tcode{true},
\item \tcode{T1} is \tcode{true}.
\end{itemize}
)";
    CHECK(latex::render_to_string(item).find(expected) != std::string::npos);
}

TEST_CASE("conjuncts - itemize survives the IR round trip") {
    DescriptionElement element;
    element.kind = ElementKind::Constraints;
    render_into(traits(4), element);

    Document doc;
    SpecItem item;
    item.descr.elements.push_back(std::move(element));
    doc.nodes.push_back(std::move(item));

    const std::string json   = emit_json(doc);
    const auto        parsed = parse_document(json);
    REQUIRE(parsed.has_value());

    CHECK(emit_json(*parsed) == json);

    const auto& round_tripped = std::get<SpecItem>(parsed->nodes[0]).descr.elements[0];
    REQUIRE(round_tripped.itemize.has_value());
    CHECK(round_tripped.itemize->items.size() == 4);
}
