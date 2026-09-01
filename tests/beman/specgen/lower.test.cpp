// tests/beman/specgen/lower.test.cpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/lower.hpp>
#include <beman/specgen/lower.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>

using namespace beman::specgen::lowering;
namespace grammar = beman::specgen::grammar;
namespace ir      = beman::specgen::ir;

namespace {

// Parse-then-lower: the path the front end will take.
Lowered lower_text(std::string_view markup) {
    auto parsed = grammar::parse_docblock(markup);
    REQUIRE(parsed.ok());
    return lower(parsed.block);
}

const std::string& code_of(const ir::Inline& in) { return std::get<ir::CodeInline>(in).code.text; }
const std::string& text_of(const ir::Inline& in) { return std::get<ir::TextInline>(in).text; }

} // namespace

TEST_CASE("lower - HeaderIsIdempotent") {
    // Placeholder: verifies header re-inclusion safety and build coherency.
    // This test always passes if the file compiles.
    REQUIRE(true);
}

TEST_CASE("lower - value_or shape: mandates prose + effects-equiv placeholder") {
    auto out = lower_text("//! \\mandates `is_copy_constructible_v<T>` is `true`.\n"
                          "//! \\effects-equiv\n");

    REQUIRE(out.descr.elements.size() == 2);

    // Canonical order puts Mandates before Effects regardless of authoring.
    const auto& mandates = out.descr.elements[0];
    CHECK(mandates.kind == ir::ElementKind::Mandates);
    REQUIRE(mandates.paragraphs.size() == 1);
    const auto& p = mandates.paragraphs[0];
    REQUIRE(p.size() == 4);
    CHECK(code_of(p[0]) == "is_copy_constructible_v<T>");
    CHECK(text_of(p[1]) == " is ");
    CHECK(code_of(p[2]) == "true");
    CHECK(text_of(p[3]) == ".");
    // Spans stay empty here; resolution is the front end's job.
    CHECK(std::get<ir::CodeInline>(p[0]).code.spans.empty());
    CHECK(!mandates.equivalent.has_value());

    // The extraction marker becomes an Effects element holding an empty
    // EquivalentTo — the slot the front end fills from the definition body.
    const auto& effects = out.descr.elements[1];
    CHECK(effects.kind == ir::ElementKind::Effects);
    CHECK(effects.paragraphs.empty());
    REQUIRE(effects.equivalent.has_value());
    CHECK(effects.equivalent->code.text.empty());
    CHECK(effects.equivalent->code.spans.empty());

    CHECK(out.directives.effects_equiv);
    CHECK(!out.directives.returns_equiv);
}

TEST_CASE("lower - emplace shape: several elements, canonical order enforced") {
    // Authored deliberately out of canonical order.
    auto out = lower_text("//! \\returns A reference to the new contained value.\n"
                          "//! \\throws Any exception thrown by the initialization.\n"
                          "//! \\effects Destroys the contained value if there is one.\n"
                          "//! \\mandates `is_constructible_v<T, Args...>` is `true`.\n");

    REQUIRE(out.descr.elements.size() == 4);
    CHECK(out.descr.elements[0].kind == ir::ElementKind::Mandates);
    CHECK(out.descr.elements[1].kind == ir::ElementKind::Effects);
    CHECK(out.descr.elements[2].kind == ir::ElementKind::Returns);
    CHECK(out.descr.elements[3].kind == ir::ElementKind::Throws);

    REQUIRE(out.descr.elements[1].paragraphs.size() == 1);
    REQUIRE(out.descr.elements[1].paragraphs[0].size() == 1);
    CHECK(text_of(out.descr.elements[1].paragraphs[0][0]) == "Destroys the contained value if there is one.");
}

TEST_CASE("lower - hardened preconditions remain distinct and precede effects") {
    auto out = lower_text("//! \\effects Performs the operation.\n"
                          "//! \\hardexpects `*this` contains a value.\n");

    REQUIRE(out.descr.elements.size() == 2);
    CHECK(out.descr.elements[0].kind == ir::ElementKind::HardExpects);
    CHECK(out.descr.elements[1].kind == ir::ElementKind::Effects);
}

TEST_CASE("lower - multi-paragraph prose survives") {
    auto out = lower_text("//! \\effects First paragraph.\n"
                          "//!\n"
                          "//! Second paragraph.\n");
    REQUIRE(out.descr.elements.size() == 1);
    REQUIRE(out.descr.elements[0].paragraphs.size() == 2);
    CHECK(text_of(out.descr.elements[0].paragraphs[0][0]) == "First paragraph.");
    CHECK(text_of(out.descr.elements[0].paragraphs[1][0]) == "Second paragraph.");
}

TEST_CASE("lower - authored items and references become semantic IR") {
    auto out = lower_text("//! \\constraints All of:\n"
                          "//! \\item `A` is `true`,\n"
                          "//! \\item `B` follows \\iref{external.requirements}.\n");
    REQUIRE(out.descr.elements.size() == 1);
    const ir::DescriptionElement& constraints = out.descr.elements.front();
    REQUIRE(constraints.paragraphs.size() == 1);
    CHECK(text_of(constraints.paragraphs.front().front()) == "All of:");
    REQUIRE(constraints.itemize.has_value());
    REQUIRE(constraints.itemize->items.size() == 2);
    CHECK(code_of(constraints.itemize->items.at(0).at(0)) == "A");
    CHECK(text_of(constraints.itemize->items.at(0).back()) == ",");
    REQUIRE(constraints.itemize->items.at(1).size() == 4);
    CHECK(code_of(constraints.itemize->items.at(1).at(0)) == "B");
    CHECK(text_of(constraints.itemize->items.at(1).at(1)) == " follows ");
    CHECK(std::get<ir::RefInline>(constraints.itemize->items.at(1).at(2)).stable_name == "external.requirements");
    CHECK(text_of(constraints.itemize->items.at(1).at(3)) == ".");
}

TEST_CASE("lower - authored two-dimensional tables become semantic IR") {
    auto out = lower_text("//! \\effects\n"
                          "//! \\lib2dtab2[optional.assign.copy]{Assignment effects}\n"
                          "//! \\column `*this` has a value\n"
                          "//! \\column `*this` has no value\n"
                          "//! \\row `rhs` has a value\n"
                          "//! \\cell assign `*rhs`.\n"
                          "//! \\cell initialize from `*rhs`.\n"
                          "//! \\endlib2dtab2\n");
    REQUIRE(out.descr.elements.size() == 1);
    const auto& table = out.descr.elements.front().table;
    REQUIRE(table.has_value());
    CHECK(table->stable_name == "optional.assign.copy");
    CHECK(text_of(table->caption.front()) == "Assignment effects");
    CHECK(code_of(table->column1.front()) == "*this");
    REQUIRE(table->rows.size() == 1);
    CHECK(code_of(table->rows.front().header.front()) == "rhs");
    CHECK(code_of(table->rows.front().cell2.at(1)) == "*rhs");
}

TEST_CASE("lower - returns-equiv produces a Returns placeholder") {
    auto out = lower_text("//! \\returns-equiv\n");
    REQUIRE(out.descr.elements.size() == 1);
    CHECK(out.descr.elements[0].kind == ir::ElementKind::Returns);
    REQUIRE(out.descr.elements[0].equivalent.has_value());
    CHECK(out.descr.elements[0].equivalent->code.text.empty());
    CHECK(out.directives.returns_equiv);
}

TEST_CASE("lower - directives are carried out of band, not into the IR") {
    auto out = lower_text("//! \\expos(has-value)\n"
                          "//! \\merge\n"
                          "//! \\constraints-in-decl\n"
                          "//! \\at optional.ctor\n"
                          "//! \\freestanding\n"
                          "//! \\freestanding-deleted\n");

    // None of this is wording, so none of it reaches the IR.
    CHECK(out.descr.elements.empty());

    CHECK(out.directives.expos);
    REQUIRE(out.directives.expos_name.has_value());
    CHECK(*out.directives.expos_name == "has-value");
    CHECK(out.directives.merge);
    CHECK(out.directives.constraints_in_decl);
    REQUIRE(out.directives.at_anchor.has_value());
    CHECK(*out.directives.at_anchor == "optional.ctor");
    CHECK(out.directives.freestanding);
    CHECK(out.directives.freestanding_deleted);

    CHECK(!out.directives.omit);
    CHECK(!out.directives.describe);
    CHECK(!out.directives.also);
    CHECK(!out.directives.seebelow);
}

TEST_CASE("lower - empty block lowers to nothing") {
    auto out = lower_text("");
    CHECK(out.descr.elements.empty());
    CHECK(!out.directives.expos);
}

TEST_CASE("lower - exposid name derivation") {
    CHECK(exposid_name("val_") == "val");
    CHECK(exposid_name("has_value_") == "has-value");
    CHECK(exposid_name("value") == "value");
    CHECK(exposid_name("some_long_name__") == "some-long-name");
    // Degenerate inputs must not misbehave.
    CHECK(exposid_name("") == "");
    CHECK(exposid_name("___") == "");
    CHECK(exposid_name("_leading") == "-leading");
}
