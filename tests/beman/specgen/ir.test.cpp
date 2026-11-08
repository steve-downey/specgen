// tests/beman/specgen/ir.test.cpp                                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/ir.hpp>
#include <beman/specgen/ir.hpp> // Re-inclusion verification

#include <beman/specgen/body_test.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace beman::specgen::ir;

TEST_CASE("ir - HeaderIsIdempotent") {
    // Placeholder: verifies header re-inclusion safety and build coherency.
    // This test always passes if the file compiles.
    REQUIRE(true);
}

TEST_CASE("ir - element names round-trip") {
    for (int i = 0; i <= static_cast<int>(ElementKind::Recommended); ++i) {
        auto kind = static_cast<ElementKind>(i);
        auto name = element_name(kind);
        CHECK(name != "?");
        auto back = element_from_name(name);
        CHECK(back.has_value());
        CHECK(*back == kind);
    }
    CHECK(!element_from_name("bogus").has_value());
}

TEST_CASE("ir - disposition names round-trip") {
    for (int i = 0; i <= static_cast<int>(Disposition::Undocumented); ++i) {
        auto disposition = static_cast<Disposition>(i);
        auto name        = disposition_name(disposition);
        CHECK(name != "?");
        auto back = disposition_from_name(name);
        CHECK(back.has_value());
        CHECK(*back == disposition);
    }
    CHECK(!disposition_from_name("bogus").has_value());
}

TEST_CASE("ir - canonicalize sorts into structure.specifications order") {
    ItemDescr d;
    d.elements.push_back(body_test::element(ElementKind::Returns, {}, {}));
    d.elements.push_back(body_test::element(ElementKind::Mandates, {}, {}));
    d.elements.push_back(body_test::element(ElementKind::Effects, {}, {}));
    canonicalize(d);
    REQUIRE(d.elements.size() == 3);
    CHECK(d.elements[0].kind == ElementKind::Mandates);
    CHECK(d.elements[1].kind == ElementKind::Effects);
    CHECK(d.elements[2].kind == ElementKind::Returns);
}

TEST_CASE("ir - canonicalize sorts a derived element before an authored one of the same kind") {
    // Design §5.2: "derived conjuncts first, authored prose
    // appended". The authored element is pushed first here (the order
    // the front end builds them in -- the docblock is lowered before the
    // derivation runs), so this also confirms canonicalize actually
    // reorders rather than merely leaving a pre-sorted input alone.
    DescriptionElement authored;
    authored.kind = ElementKind::Mandates;

    DescriptionElement derived;
    derived.kind    = ElementKind::Mandates;
    derived.derived = true;

    ItemDescr d;
    d.elements.push_back(authored);
    d.elements.push_back(derived);
    canonicalize(d);

    REQUIRE(d.elements.size() == 2);
    CHECK(d.elements[0].derived);
    CHECK_FALSE(d.elements[1].derived);
}

TEST_CASE("ir - member kind names round-trip") {
    for (const MemberKind kind : {MemberKind::Function, MemberKind::Data, MemberKind::Alias}) {
        const auto name = member_kind_name(kind);
        CHECK(name != "?");
        const auto back = member_kind_from_name(name);
        REQUIRE(back.has_value());
        CHECK(*back == kind);
    }
    CHECK(!member_kind_from_name("bogus").has_value());
}

TEST_CASE("ir - change names round-trip") {
    for (const Change change : {Change::Same, Change::Added, Change::Removed}) {
        const auto name = change_name(change);
        CHECK(name != "?");
        const auto back = change_from_name(name);
        REQUIRE(back.has_value());
        CHECK(*back == change);
    }
    CHECK(change_name(Change::Same) == "same");
    CHECK(change_name(Change::Added) == "added");
    CHECK(change_name(Change::Removed) == "removed");
    CHECK(!change_from_name("modified").has_value());
}

TEST_CASE("ir - every node and element is unmarked by default") {
    CHECK(Section{}.change == Change::Same);
    CHECK(Synopsis{}.change == Change::Same);
    CHECK(SpecItem{}.change == Change::Same);
    CHECK(FreeParagraph{}.change == Change::Same);
    CHECK(DescriptionElement{}.change == Change::Same);
    CHECK(CodeText{}.changes.empty());
}

TEST_CASE("ir - change_of, with_change and all_marked reach the mark on a node of any kind") {
    std::vector<Node> nodes = {Section{"s", "S", {}}, Synopsis{}, SpecItem{}, FreeParagraph{}};
    for (const Node& node : nodes)
        CHECK(change_of(node) == Change::Same);
    for (const Node& node : all_marked(nodes, Change::Removed))
        CHECK(change_of(node) == Change::Removed);
    CHECK(change_of(with_change(nodes[0], Change::Added)) == Change::Added);
    CHECK(std::get<Section>(with_change(nodes[0], Change::Added)).stable_name == "s");
}

TEST_CASE("ir - block_change reads the mark on a block of either body") {
    CHECK(block_change(BodyBlock{BodyParagraph{{TextInline{"p"}}, false, Change::Added}}) == Change::Added);
    CHECK(block_change(BodyBlock{Itemize{{}, Change::Removed}}) == Change::Removed);
    CHECK(block_change(BodyBlock{Note{{}, Change::Added}}) == Change::Added);
    CHECK(block_change(BodyBlock{EquivalentTo{{"x;", {}}}}) == Change::Same);
    CHECK(block_change(InformativeBlock{AuthoredCode{{"x;", {}}, Change::Removed}}) == Change::Removed);
    CHECK(change_name(Change::Elided) == "elided");
    CHECK(change_from_name("elided") == Change::Elided);
}
