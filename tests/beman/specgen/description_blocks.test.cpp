// tests/beman/specgen/description_blocks.test.cpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/backend/latex.hpp>
#include <beman/specgen/backend/mpark.hpp>
#include <beman/specgen/backend/org.hpp>
#include <beman/specgen/docblock.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/ir_json.hpp>
#include <beman/specgen/lower.hpp>
#include <beman/specgen/validate/validate.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>

namespace grammar = beman::specgen::grammar;
namespace ir      = beman::specgen::ir;
namespace backend = beman::specgen::backend;

namespace {
std::size_t occurrences(std::string_view text, std::string_view marker);
std::size_t occurrences(std::string_view text, std::string_view marker) {
    std::size_t result = 0;
    for (auto pos = text.find(marker); pos != std::string_view::npos; pos = text.find(marker, pos + marker.size()))
        ++result;
    return result;
}
} // namespace

TEST_CASE("description blocks - examples preserve code and do not number informative paragraphs") {
    const auto parsed = grammar::parse_docblock(R"(//! \effects Performs an operation.
//! \note An explanation with `T` and \iref{example.general}.
//!
//! Another informative paragraph.
//! \item An informative list entry.
//! \endnote
//! \example A worked example:
//! \codeblock
//!   run();
//!
//! \effects This command is code payload.
//! \endcodeblock
//! The example continues after its code.
//! \endexample
//! The normative paragraph continues here.
//!
//! A second normative paragraph.
)");
    REQUIRE(parsed.ok());
    REQUIRE(parsed.diags.empty());
    ir::SpecItem item;
    item.decl.signatures.push_back({"void run();", {}});
    item.descr       = beman::specgen::lowering::lower(parsed.block).descr;
    const auto& body = item.descr.elements.front().body;
    REQUIRE(body.size() == 5);
    const auto& example = std::get<ir::Example>(body[2]);
    REQUIRE(example.body.size() == 3);
    CHECK(std::get<ir::AuthoredCode>(example.body[1]).code.text ==
          "  run();\n\n\\effects This command is code payload.");
    CHECK(std::get<ir::BodyParagraph>(body[3]).continuation);
    CHECK_FALSE(std::get<ir::BodyParagraph>(body[4]).continuation);

    const auto latex = backend::latex::render_to_string(item);
    CHECK(occurrences(latex, "\\pnum\n") == 2);
    CHECK(latex.contains("\\begin{note}"));
    CHECK(latex.contains("\\begin{example}"));
    CHECK_FALSE(latex.contains("Equivalent to:"));
    const auto markdown = backend::mpark::render_to_string(item);
    CHECK(occurrences(markdown, "[#]{.pnum}") == 2);
    CHECK_FALSE(markdown.contains("[#.#]{.pnum}"));
    CHECK(markdown.contains("::: note"));
    CHECK(markdown.contains("::: example"));
    const auto org = backend::org::render_to_string(item);
    CHECK(occurrences(org, "#+begin_pnum") == 2);
    CHECK(org.contains("#+begin_note"));
    CHECK(org.contains("#+begin_wgexample"));
    CHECK_FALSE(org.contains("#+begin_example"));

    const auto emitted  = ir::emit_json(item);
    const auto restored = ir::parse_item(emitted);
    REQUIRE(restored.has_value());
    CHECK(ir::emit_json(*restored) == emitted);
    CHECK(backend::latex::render_to_string(*restored) == latex);
}

TEST_CASE("description blocks - malformed informative delimiters retain positioned diagnostics") {
    for (const auto& [source, message, line] : {
             std::tuple{"//! \\effects Text.\n//! \\note Unclosed.\n", "missing \\endnote", 2},
             std::tuple{"//! \\effects Text.\n//! \\example Unclosed.\n", "missing \\endexample", 2},
             std::tuple{"//! \\effects Text.\n//! \\codeblock\n//! x();\n", "missing \\endcodeblock", 2},
             std::tuple{"//! \\effects Text.\n//! \\endnote\n", "\\endnote appears outside its block", 2},
             std::tuple{
                 "//! \\note Outer.\n//! \\example Inner.\n//! \\endnote\n", "notes and examples cannot nest", 2},
             std::tuple{"//! \\example Text.\n//! \\endnote\n", "expected \\endexample, found \\endnote", 2},
         }) {
        const auto parsed = grammar::parse_docblock(source);
        CHECK_FALSE(parsed.ok());
        CHECK(std::ranges::any_of(parsed.diags, [&](const grammar::Diagnostic& diagnostic) {
            return diagnostic.line == line && diagnostic.message == message;
        }));
    }
}

TEST_CASE("description blocks - informative tables retain cells and surrounding prose") {
    const auto parsed = grammar::parse_docblock(R"(//! \effects Normative wording.
//! \note Table explanation.
//! \libtab2[example.flat]{Flat table}
//! \column Value
//! \column Meaning
//! \row A
//! \cell B
//! \endlibtab2
//! Between tables.
//! \lib2dtab2[example.cross]{Cross table}
//! \column First
//! \column Second
//! \row Row
//! \cell C
//! \cell D
//! \endlib2dtab2
//! After both tables.
//! \endnote
//! Normative continuation.
)");
    REQUIRE(parsed.ok());
    REQUIRE(parsed.diags.empty());
    ir::SpecItem item;
    item.decl.signatures.push_back({"void f();", {}});
    item.descr       = beman::specgen::lowering::lower(parsed.block).descr;
    const auto& body = item.descr.elements.front().body;
    REQUIRE(body.size() == 3);
    const auto& note = std::get<ir::Note>(body[1]);
    REQUIRE(note.body.size() == 5);
    const auto& flat = std::get<ir::Table1D>(note.body[1]);
    REQUIRE(flat.rows.size() == 1);
    CHECK(std::get<ir::TextInline>(flat.rows.front().cell2.front()).text == "B");
    const auto& cross = std::get<ir::Table2D>(note.body[3]);
    REQUIRE(cross.rows.size() == 1);
    CHECK(std::get<ir::TextInline>(cross.rows.front().cell2.front()).text == "D");
    CHECK(std::get<ir::BodyParagraph>(note.body[4]).continuation);
    CHECK(std::get<ir::BodyParagraph>(body[2]).continuation);
    CHECK(occurrences(backend::latex::render_to_string(item), "\\pnum\n") == 1);
    CHECK(occurrences(backend::mpark::render_to_string(item), "[#]{.pnum}") == 1);
    CHECK(occurrences(backend::org::render_to_string(item), "#+begin_pnum") == 1);
    const auto emitted  = ir::emit_json(item);
    const auto restored = ir::parse_item(emitted);
    REQUIRE(restored.has_value());
    CHECK(ir::emit_json(*restored) == emitted);
}

TEST_CASE("description blocks - informative Throws text does not assert a throwing guarantee") {
    ir::SpecItem item;
    item.decl.signatures.push_back({"void f() noexcept;", {}});
    ir::DescriptionElement throws;
    throws.kind = ir::ElementKind::Throws;
    throws.body = {ir::Note{{ir::BodyParagraph{{ir::TextInline{"An informative exception discussion."}}}}}};
    item.descr.elements.push_back(throws);
    CHECK(beman::specgen::validate::validate(ir::Node{item}).empty());

    item.descr.elements.front().body.insert(item.descr.elements.front().body.begin(),
                                            ir::BodyParagraph{{ir::TextInline{"Nothing."}}});
    const auto diagnostics = beman::specgen::validate::validate(ir::Node{item});
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().message.contains("adds nothing"));
}

TEST_CASE("description blocks - informative text cannot discharge a normative drift obligation") {
    ir::SpecItem item;
    item.decl.signatures.push_back({"void f();", {}});
    const ir::Paragraph    claim{ir::CodeInline{{"C<T>", {}}}, ir::TextInline{" is "}, ir::CodeInline{{"true", {}}}};
    ir::DescriptionElement authored;
    authored.kind          = ir::ElementKind::Constraints;
    authored.conjuncts     = {claim}; // evidence retained when authored wording replaces a derivation
    authored.body          = {ir::Note{{ir::BodyParagraph{claim}}}};
    item.descr.elements    = {authored};
    const auto diagnostics = beman::specgen::validate::validate(ir::Node{item});
    CHECK(std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.contains("no authored constraints counterpart");
    }));
    item.descr.elements.back().body = {ir::BodyParagraph{claim}};
    const auto normative            = beman::specgen::validate::validate(ir::Node{item});
    CHECK(std::ranges::none_of(normative,
                               [](const auto& diagnostic) { return diagnostic.message.contains("counterpart"); }));
    CHECK(normative.empty());
}

TEST_CASE("description blocks - informative code and prose remain leakage sites") {
    ir::Document document;
    ir::Synopsis synopsis;
    synopsis.name      = "wrapper";
    synopsis.code.text = "class wrapper {};";
    synopsis.roster    = {{"hidden_", ir::Disposition::Private, ""}};
    document.nodes.push_back(synopsis);
    ir::SpecItem item;
    item.decl.signatures.push_back({"void f();", {}});
    ir::DescriptionElement element;
    element.kind = ir::ElementKind::Effects;
    element.body = {ir::Note{{ir::BodyParagraph{{ir::CodeInline{{"hidden_", {}}}}}}},
                    ir::Example{{ir::AuthoredCode{{"hidden_();", {}}}}}};
    item.descr.elements.push_back(element);
    document.nodes.push_back(item);
    const auto diagnostics = beman::specgen::validate::validate(document);
    CHECK(std::ranges::count_if(diagnostics,
                                [](const auto& diagnostic) { return diagnostic.message.contains("hidden_"); }) == 2);
}

TEST_CASE("description blocks - authored markdown fences cannot close their surrounding code block") {
    ir::SpecItem item;
    item.decl.signatures.push_back({"void f();", {}});
    item.descr.leading  = {ir::AuthoredCode{{"// A literal fence follows.\n```\nrun();", {}}}};
    const auto markdown = backend::mpark::render_to_string(item);
    CHECK(markdown.contains("````cpp\n// A literal fence follows.\n```\nrun();\n````\n"));
}

TEST_CASE("description blocks - informative markers cannot suppress or replace a declaration") {
    for (std::string_view marker : {"omit", "effects-equiv", "verbatim-synopsis", "verbatim-itemdecl"}) {
        const auto parsed = grammar::parse_docblock(std::format(R"(//! \effects Text.
//! \note An explanation.
//! \{}
//! \endnote
)",
                                                                marker));
        CHECK_FALSE(parsed.ok());
        CHECK(parsed.block.markers == grammar::Markers{});
        REQUIRE(parsed.block.elements.size() == 1);
        REQUIRE(parsed.block.elements.front().body.size() == 2);
        CHECK(std::holds_alternative<grammar::Note>(parsed.block.elements.front().body.back()));
    }
}
