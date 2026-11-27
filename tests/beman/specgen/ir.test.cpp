// tests/beman/specgen/ir.test.cpp                                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/ir.hpp>
#include <beman/specgen/ir.hpp> // Re-inclusion verification

#include <beman/specgen/body_test.hpp>

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

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

TEST_CASE("ir - a piece of a text inline takes the hints that sit in it, and joining gives them back") {
    using beman::specgen::ir::LayoutHint;
    using beman::specgen::ir::LayoutHintKind;
    using beman::specgen::ir::TextInline;
    const TextInline text{"an implementation, ",
                          {{0, LayoutHintKind::Break},
                           {8, LayoutHintKind::Hyphen},
                           {17, LayoutHintKind::Break},
                           {19, LayoutHintKind::LineBreak}}};
    // A hint precedes the byte at its offset, so one at a cut belongs to the
    // piece after it -- except at the end of the text, where there is none.
    const TextInline first = beman::specgen::ir::text_piece(text, 0, 3);
    const TextInline word  = beman::specgen::ir::text_piece(text, 3, 17);
    const TextInline rest  = beman::specgen::ir::text_piece(text, 17, 19);
    CHECK(first == TextInline{"an ", {{0, LayoutHintKind::Break}}});
    CHECK(word == TextInline{"implementation", {{5, LayoutHintKind::Hyphen}}});
    CHECK(rest == TextInline{", ", {{0, LayoutHintKind::Break}, {2, LayoutHintKind::LineBreak}}});

    TextInline joined = first;
    beman::specgen::ir::append_text(joined, word);
    beman::specgen::ir::append_text(joined, rest);
    CHECK(joined == text);

    // Without its typesetting an inline is its wording alone.
    using beman::specgen::ir::PlainInline;
    CHECK(beman::specgen::ir::without_typesetting(PlainInline{text}) ==
          PlainInline{TextInline{"an implementation, "}});
    CHECK(beman::specgen::ir::without_typesetting(
              PlainInline{beman::specgen::ir::OldConceptRef{"UnaryTypeTrait", {{5, LayoutHintKind::Hyphen}}}}) ==
          PlainInline{beman::specgen::ir::OldConceptRef{"UnaryTypeTrait"}});
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

TEST_CASE("ir - opens_paragraph says which blocks take a paragraph number of their own") {
    // Prose says it by not continuing, and an extracted body always does.
    CHECK(opens_paragraph(BodyBlock{BodyParagraph{{TextInline{"p"}}, false}}));
    CHECK_FALSE(opens_paragraph(BodyBlock{BodyParagraph{{TextInline{"p"}}, true}}));
    CHECK(opens_paragraph(BodyBlock{EquivalentTo{{"x;", {}}}}));
    // The rest belong to the paragraph before them unless they say otherwise.
    CHECK_FALSE(opens_paragraph(BodyBlock{Note{}}));
    CHECK_FALSE(opens_paragraph(BodyBlock{Example{}}));
    CHECK_FALSE(opens_paragraph(BodyBlock{Itemize{}}));
    CHECK_FALSE(opens_paragraph(BodyBlock{AuthoredCode{}}));
    CHECK_FALSE(opens_paragraph(BodyBlock{Table1D{}}));
    CHECK_FALSE(opens_paragraph(BodyBlock{Table2D{}}));
    CHECK(opens_paragraph(BodyBlock{Note{{}, Change::Same, true}}));
    CHECK(opens_paragraph(BodyBlock{Example{{}, Change::Same, true}}));
    CHECK(opens_paragraph(BodyBlock{Itemize{{}, Change::Same, true}}));
    CHECK(opens_paragraph(BodyBlock{AuthoredCode{{"x;", {}}, Change::Same, true}}));
    CHECK(opens_paragraph(BodyBlock{Table1D{.opens_paragraph = true}}));
    CHECK(opens_paragraph(BodyBlock{Table2D{.opens_paragraph = true}}));
}

TEST_CASE("ir - a list that opens a paragraph of its own is not joined to the list before it") {
    const DescriptionBody joined = coalesce_itemizations({Itemize{{{TextInline{"a"}}}}, Itemize{{{TextInline{"b"}}}}});
    REQUIRE(joined.size() == 1);
    CHECK(std::get<Itemize>(joined[0]).items.size() == 2);

    const DescriptionBody apart =
        coalesce_itemizations({Itemize{{{TextInline{"a"}}}}, Itemize{{{TextInline{"b"}}}, Change::Same, true}});
    REQUIRE(apart.size() == 2);
    CHECK(std::get<Itemize>(apart[1]).opens_paragraph);
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

namespace {
Inline text(std::string value) { return TextInline{.text = std::move(value)}; }
Inline code(std::string value) { return CodeInline{.code = CodeText{.text = std::move(value), .spans = {}}}; }
} // namespace

TEST_CASE("ir - let_bindings reads only the exact \"Let `U` be `X`.\" shape") {
    const DescriptionBody leading = beman::specgen::test::prose({
        {text("Let "), code("U"), text(" be "), code("invoke_result_t<F>"), text(".")},
        {text("Let "), code("V"), text(" be the type "), code("T"), text(".")},
    });
    CHECK(let_bindings(leading) == std::map<std::string, std::string>{{"U", "invoke_result_t<F>"}});
}

TEST_CASE("ir - let_sentence reads one, two, and three bindings in the draft's joined sentence") {
    using Bindings = std::vector<std::pair<std::string, std::string>>;
    const Paragraph one{text("Let "), code("U"), text(" be "), code("X"), text(".")};
    const Paragraph two{text("Let "),
                        code("U"),
                        text(" be "),
                        code("X"),
                        text(" and "),
                        code("V"),
                        text(" be "),
                        code("Y"),
                        text(".")};
    const Paragraph three{text("Let "),
                          code("U"),
                          text(" be "),
                          code("X"),
                          text(", "),
                          code("V"),
                          text(" be "),
                          code("Y"),
                          text(", and "),
                          code("W"),
                          text(" be "),
                          code("Z"),
                          text(".")};
    CHECK(let_sentence(one) == Bindings{{"U", "X"}});
    CHECK(let_sentence(two) == Bindings{{"U", "X"}, {"V", "Y"}});
    CHECK(let_sentence(three) == Bindings{{"U", "X"}, {"V", "Y"}, {"W", "Z"}});

    // Two bindings take " and ", not ", and"; three take ", " then ", and ".
    const Paragraph comma_two{text("Let "),
                              code("U"),
                              text(" be "),
                              code("X"),
                              text(", and "),
                              code("V"),
                              text(" be "),
                              code("Y"),
                              text(".")};
    CHECK_FALSE(let_sentence(comma_two).has_value());
    const Paragraph loose{text("Let "), code("U"), text(" be the type "), code("X"), text(".")};
    CHECK_FALSE(let_sentence(loose).has_value());

    const DescriptionBody leading = beman::specgen::test::prose({two});
    CHECK(let_bindings(leading) == std::map<std::string, std::string>{{"U", "X"}, {"V", "Y"}});
    CHECK(let_names(leading) == std::set<std::string>{"U", "V"});
}

TEST_CASE("ir - let_names reads every paragraph that opens \"Let `U`\", in any shape") {
    const DescriptionBody leading = beman::specgen::test::prose({
        {text("Let "), code("U"), text(" be "), code("invoke_result_t<F>"), text(".")},
        {text("Let "), code("V"), text(" be the type "), code("T"), text(".")},
        {code("W"), text(" names the result.")},
        {text("Let the result be "), code("X"), text(".")},
    });
    CHECK(let_names(leading) == std::set<std::string>{"U", "V"});
}
