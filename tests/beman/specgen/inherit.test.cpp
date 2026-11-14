// tests/beman/specgen/inherit.test.cpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/inherit.hpp>
#include <beman/specgen/inherit.hpp> // Re-inclusion verification

#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace beman::specgen::ir;
namespace inherit = beman::specgen::inherit;

namespace {

Paragraph text(std::string s) { return {TextInline{std::move(s)}}; }

DescriptionElement element(ElementKind kind, std::string words) {
    DescriptionElement e;
    e.kind = kind;
    e.body = {BodyParagraph{text(std::move(words))}};
    return e;
}

SpecItem item(std::string signature, std::vector<std::string> entities, std::vector<DescriptionElement> elements) {
    SpecItem it;
    it.decl.signatures.push_back({std::move(signature), {}});
    it.decl.entities  = std::move(entities);
    it.descr.elements = std::move(elements);
    return it;
}

SpecItem marked(std::string signature, std::vector<std::string> entities, std::vector<DescriptionElement> elements = {}) {
    SpecItem it   = item(std::move(signature), std::move(entities), std::move(elements));
    it.as_draft = true;
    return it;
}

Document in_section(std::string stable, std::vector<Node> children) {
    Document doc;
    doc.nodes.push_back(Section{std::move(stable), "Title", std::move(children)});
    return doc;
}

const SpecItem& item_at(const Document& doc, std::size_t index) {
    return std::get<SpecItem>(std::get<Section>(doc.nodes.front()).children.at(index));
}

const DescriptionElement* element_of(const SpecItem& it, ElementKind kind) {
    const auto found = std::ranges::find(it.descr.elements, kind, &DescriptionElement::kind);
    return found == it.descr.elements.end() ? nullptr : &*found;
}

} // namespace

TEST_CASE("inherit - an item takes the description of the baseline item it pairs with") {
    const Document baseline = in_section(
        "s",
        {item("void f();",
              {"f"},
              {element(ElementKind::Effects, "Does f."), element(ElementKind::Returns, "Nothing.")})});
    Document current = in_section("s", {marked("constexpr void f();", {"f"})});

    REQUIRE(inherit::has_unresolved(current));
    REQUIRE(inherit::resolve(current, baseline).empty());
    CHECK_FALSE(inherit::has_unresolved(current));

    const SpecItem& resolved = item_at(current, 0);
    CHECK(resolved.decl.signatures.front().text == "constexpr void f();");
    CHECK(resolved.descr == item("", {}, {element(ElementKind::Effects, "Does f."), element(ElementKind::Returns, "Nothing.")}).descr);
}

TEST_CASE("inherit - an overload pairs with the baseline overload that reads most like it") {
    const Document baseline = in_section("s",
                                         {item("void f(int);", {"f"}, {element(ElementKind::Effects, "Int.")}),
                                          item("void f(const string&);", {"f"}, {element(ElementKind::Effects, "String.")})});
    // The header lists the overloads in the other order, and adds a specifier.
    Document current = in_section("s",
                                  {marked("constexpr void f(const string&);", {"f"}),
                                   marked("constexpr void f(int);", {"f"})});

    REQUIRE(inherit::resolve(current, baseline).empty());
    CHECK(element_of(item_at(current, 0), ElementKind::Effects)->body == element(ElementKind::Effects, "String.").body);
    CHECK(element_of(item_at(current, 1), ElementKind::Effects)->body == element(ElementKind::Effects, "Int.").body);
}

TEST_CASE("inherit - an item takes a baseline item once") {
    const Document baseline = in_section("s", {item("void f();", {"f"}, {element(ElementKind::Effects, "Once.")})});
    Document       current  = in_section("s", {marked("void f();", {"f"}), marked("void f();", {"f"})});

    const auto findings = inherit::resolve(current, baseline);
    REQUIRE(findings.size() == 1);
    CHECK(findings.front().severity == beman::specgen::Severity::Error);
    CHECK(findings.front().message.contains("no item of the baseline"));
    CHECK(inherit::has_unresolved(current));
}

TEST_CASE("inherit - an authored element replaces the baseline's element of its kind") {
    const Document baseline = in_section(
        "s",
        {item("void f();",
              {"f"},
              {element(ElementKind::Effects, "Old effects."), element(ElementKind::Remarks, "Kept remarks.")})});
    Document current =
        in_section("s", {marked("void f();", {"f"}, {element(ElementKind::Effects, "New effects.")})});

    REQUIRE(inherit::resolve(current, baseline).empty());
    const SpecItem& resolved = item_at(current, 0);
    CHECK(element_of(resolved, ElementKind::Effects)->body == element(ElementKind::Effects, "New effects.").body);
    CHECK(element_of(resolved, ElementKind::Remarks)->body == element(ElementKind::Remarks, "Kept remarks.").body);
}

TEST_CASE("inherit - a derivation's conjuncts move onto the inherited element and its words stay") {
    const Document baseline = in_section(
        "s", {item("void f();", {"f"}, {element(ElementKind::Constraints, "`C<T>` is `true`.")})});
    DescriptionElement derived = element(ElementKind::Constraints, "derived words");
    derived.derived            = true;
    derived.conjuncts          = {text("C<T> is true")};
    Document current           = in_section("s", {marked("void f();", {"f"}, {derived})});

    REQUIRE(inherit::resolve(current, baseline).empty());
    const DescriptionElement* constraints = element_of(item_at(current, 0), ElementKind::Constraints);
    REQUIRE(constraints != nullptr);
    CHECK(constraints->body == element(ElementKind::Constraints, "`C<T>` is `true`.").body);
    CHECK(constraints->conjuncts == std::vector<Paragraph>{text("C<T> is true")});
    CHECK_FALSE(constraints->derived);
}

TEST_CASE("inherit - a derivation with no baseline counterpart is new wording and stays") {
    const Document baseline = in_section("s", {item("void f();", {"f"}, {element(ElementKind::Effects, "Does f.")})});
    DescriptionElement derived = element(ElementKind::Constraints, "derived words");
    derived.derived            = true;
    Document current           = in_section("s", {marked("void f();", {"f"}, {derived})});

    REQUIRE(inherit::resolve(current, baseline).empty());
    const DescriptionElement* constraints = element_of(item_at(current, 0), ElementKind::Constraints);
    REQUIRE(constraints != nullptr);
    CHECK(constraints->derived);
}

TEST_CASE("inherit - a covers claim lands on the inherited element, or is an error without one") {
    const Document baseline = in_section(
        "s", {item("void f();", {"f"}, {element(ElementKind::Constraints, "`C<T>` is `true`.")})});

    Document with = in_section("s", {marked("void f();", {"f"})});
    std::get<SpecItem>(std::get<Section>(with.nodes.front()).children.front()).covers = {
        {ElementKind::Constraints, text("P is true")}};
    REQUIRE(inherit::resolve(with, baseline).empty());
    CHECK(element_of(item_at(with, 0), ElementKind::Constraints)->coverage == std::vector<Paragraph>{text("P is true")});
    CHECK(item_at(with, 0).covers.empty());

    Document without = in_section("s", {marked("void f();", {"f"})});
    std::get<SpecItem>(std::get<Section>(without.nodes.front()).children.front()).covers = {
        {ElementKind::Mandates, text("P is true")}};
    const auto findings = inherit::resolve(without, baseline);
    REQUIRE(findings.size() == 1);
    CHECK(findings.front().message.contains("mandates"));
}

TEST_CASE("inherit - an item with no counterpart is an error and is left as it was") {
    const Document baseline = in_section("s", {item("void g();", {"g"}, {element(ElementKind::Effects, "Does g.")})});
    Document       current  = in_section("s", {marked("void f();", {"f"})});

    const auto findings = inherit::resolve(current, baseline);
    REQUIRE(findings.size() == 1);
    CHECK(findings.front().severity == beman::specgen::Severity::Error);
    CHECK(findings.front().where == "void f();");
    CHECK(findings.front().message.contains("section s"));
    CHECK(item_at(current, 0).as_draft);
}

TEST_CASE("inherit - a section the baseline lacks leaves its marked items without a pair") {
    Document current = in_section("new.section", {marked("void f();", {"f"})});
    const auto findings = inherit::resolve(current, Document{});
    REQUIRE(findings.size() == 1);
    CHECK(findings.front().message.contains("\\asdraft"));
}

TEST_CASE("inherit - an unmarked item is not touched") {
    const Document baseline = in_section("s", {item("void f();", {"f"}, {element(ElementKind::Effects, "Base.")})});
    Document current = in_section("s", {item("void f();", {"f"}, {element(ElementKind::Effects, "Mine.")})});
    const Document before = current;

    CHECK(inherit::resolve(current, baseline).empty());
    CHECK(current == before);
}
