// tests/beman/specgen/frontend/class_at.test.cpp                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/frontend.hpp>
#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>
#include <string>
#include <variant>
#include <vector>

namespace frontend = beman::specgen::frontend;
namespace ir       = beman::specgen::ir;

namespace {
const std::string kCorpus = std::string(BEMAN_SPECGEN_CORPUS_DIR);

const ir::Section* find_section(const std::vector<ir::Node>& nodes, std::string_view stable) {
    for (const ir::Node& node : nodes) {
        const auto* section = std::get_if<ir::Section>(&node);
        if (section == nullptr)
            continue;
        if (section->stable_name == stable)
            return section;
        if (const ir::Section* nested = find_section(section->children, stable))
            return nested;
    }
    return nullptr;
}

std::string paragraph_text(const ir::Paragraph& para) {
    std::string out;
    for (const ir::Inline& in : para) {
        if (const auto* text = std::get_if<ir::TextInline>(&in))
            out += text->text;
        else if (const auto* code = std::get_if<ir::CodeInline>(&in))
            out += code->code.text;
        else if (const auto* concept_ref = std::get_if<ir::ConceptRef>(&in))
            out += concept_ref->name;
    }
    return out;
}

// A section's own children as their variant indexes, which is how "same node
// kinds, same order" is asked of two sections without naming either's
// contents.
std::vector<std::size_t> shape(const ir::Section& section) {
    return section.children | std::views::transform([](const ir::Node& node) { return node.index(); }) |
           std::ranges::to<std::vector>();
}

// The one description-only item in a section: a class's own description is a
// SpecItem with no signatures (design §6), and no other node in this fixture's
// routed sections is one.
const ir::SpecItem* only_item(const ir::Section& section) {
    const ir::SpecItem* found = nullptr;
    for (const ir::Node& node : section.children)
        if (const auto* item = std::get_if<ir::SpecItem>(&node))
            found = item;
    return found;
}

const ir::Synopsis& region_synopsis(const ir::Document& document) {
    const ir::Section* syn = find_section(document.nodes, "box.syn");
    REQUIRE(syn != nullptr);
    const auto* found = std::get_if<ir::Synopsis>(&syn->children.front());
    REQUIRE(found != nullptr);
    return *found;
}
} // namespace

// The gap issue #98 names: issue #41 gave a folded-in class's own wording a
// place to land -- beside its synopsis -- but never wired `\at` to redirect
// it, and issue #69's route is a namespace entity's. Both halves travel now:
// the class-scope `static_assert` paragraph (design §5.2) and the class's own
// description (issue #18), in that order.
TEST_CASE("an `\\at` on a folded-in class routes both halves of its own wording") {
    const auto built = frontend::build_document(kCorpus + "/spec_gathered_class_at.hpp");
    REQUIRE(built.has_value());

    const ir::Section* crate = find_section(built->document.nodes, "box.crate");
    REQUIRE(crate != nullptr);
    REQUIRE(crate->children.size() == 2);

    const auto* general = std::get_if<ir::FreeParagraph>(&crate->children[0]);
    REQUIRE(general != nullptr);
    const auto* item = std::get_if<ir::SpecItem>(&crate->children[1]);
    REQUIRE(item != nullptr);
    CHECK(item->decl.signatures.empty());
    REQUIRE(item->descr.elements.size() == 1);
    CHECK(item->descr.elements.front().kind == ir::ElementKind::Remarks);

    // And [box.syn] no longer carries either: the routed class contributes
    // its declaration to the gathered synopsis and nothing else.
    const ir::Section* syn = find_section(built->document.nodes, "box.syn");
    REQUIRE(syn != nullptr);
    CHECK(std::get<ir::Synopsis>(syn->children.front()).code.text.contains("crate"));
    CHECK(std::ranges::none_of(syn->children, [](const ir::Node& node) {
        const auto* paragraph = std::get_if<ir::FreeParagraph>(&node);
        return paragraph != nullptr && paragraph_text(paragraph->text).contains("crate");
    }));
}

// Issue #41's behaviour, which the route must not disturb: without an `\at`
// the class's own wording still stands beside its synopsis, in whatever
// section is open.
TEST_CASE("a folded-in class with no `\\at` keeps its wording beside its synopsis") {
    const auto built = frontend::build_document(kCorpus + "/spec_gathered_class_at.hpp");
    REQUIRE(built.has_value());

    const ir::Section* syn = find_section(built->document.nodes, "box.syn");
    REQUIRE(syn != nullptr);
    // Its own wording, without the `\rSec3` subclauses that nest inside it:
    // the synopsis, then `crumb`'s paragraph, then `crumb`'s description --
    // and nothing else, every other class in the region having routed away.
    const auto wording =
        syn->children |
        std::views::filter([](const ir::Node& node) { return !std::holds_alternative<ir::Section>(node); }) |
        std::ranges::to<std::vector>();
    REQUIRE(wording.size() == 3);
    CHECK(std::holds_alternative<ir::Synopsis>(wording[0]));

    const auto* general = std::get_if<ir::FreeParagraph>(&wording[1]);
    REQUIRE(general != nullptr);
    CHECK(paragraph_text(general->text).contains("crumb"));

    const auto* item = std::get_if<ir::SpecItem>(&wording[2]);
    REQUIRE(item != nullptr);
    CHECK(item->decl.signatures.empty());
    REQUIRE(item->descr.elements.size() == 1);
    CHECK(item->descr.elements.front().kind == ir::ElementKind::Remarks);
}

// The two halves are independent: a class may have only the derived paragraph
// (a `static_assert` and no docblock prose) or only the authored description.
TEST_CASE("each half of a routed class's own wording travels on its own") {
    const auto built = frontend::build_document(kCorpus + "/spec_gathered_class_at.hpp");
    REQUIRE(built.has_value());

    // `pallet` is a class *template*, so this is also the ClassTemplateDecl
    // arm of classify() reading the marker.
    const ir::Section* pallet = find_section(built->document.nodes, "box.pallet");
    REQUIRE(pallet != nullptr);
    REQUIRE(pallet->children.size() == 1);
    const auto* general = std::get_if<ir::FreeParagraph>(&pallet->children.front());
    REQUIRE(general != nullptr);
    CHECK(paragraph_text(general->text).contains("pallet<T>"));

    const ir::Section* label = find_section(built->document.nodes, "box.label");
    REQUIRE(label != nullptr);
    REQUIRE(label->children.size() == 1);
    const auto* item = std::get_if<ir::SpecItem>(&label->children.front());
    REQUIRE(item != nullptr);
    CHECK(item->decl.signatures.empty());
    REQUIRE(item->descr.elements.size() == 1);
    CHECK(item->descr.elements.front().kind == ir::ElementKind::Remarks);
}

// The binding property, asked of the tree rather than reasoned about: a
// routed folded-in class's wording is the same nodes in the same order as a
// class outside every region puts in its own section.
TEST_CASE("a routed folded-in class renders the shape a non-folded one does") {
    const auto built = frontend::build_document(kCorpus + "/spec_gathered_class_at.hpp");
    REQUIRE(built.has_value());

    const ir::Section* crate = find_section(built->document.nodes, "box.crate");
    const ir::Section* hinge = find_section(built->document.nodes, "box.hinge.general");
    REQUIRE(crate != nullptr);
    REQUIRE(hinge != nullptr);
    CHECK(shape(*crate) == shape(*hinge));

    const ir::SpecItem* crate_item = only_item(*crate);
    const ir::SpecItem* hinge_item = only_item(*hinge);
    REQUIRE(crate_item != nullptr);
    REQUIRE(hinge_item != nullptr);
    CHECK(crate_item->decl.signatures.size() == hinge_item->decl.signatures.size());
    REQUIRE(crate_item->descr.elements.size() == hinge_item->descr.elements.size());
    CHECK(crate_item->descr.elements.front().kind == hinge_item->descr.elements.front().kind);

    // The class outside the region routed its wording out of the section it
    // is written in, so its own synopsis stayed behind.
    const ir::Section* written = find_section(built->document.nodes, "box.hinge");
    REQUIRE(written != nullptr);
    CHECK(std::holds_alternative<ir::Synopsis>(written->children.front()));
}

// What makes a route checkable at all (design §9's dangling-route rule):
// build_tree drops a pending item whose section no `\rSec` opens, and the
// roster entry is the only record left that the request was made. Without it
// a misspelled `\at` loses the class's wording as silently as the marker
// being a no-op did.
TEST_CASE("a routed class earns a roster entry naming itself") {
    const auto built = frontend::build_document(kCorpus + "/spec_gathered_class_at.hpp");
    REQUIRE(built.has_value());

    const ir::Synopsis& gathered = region_synopsis(built->document);
    const auto          routed   = gathered.roster | std::views::filter([](const ir::SynopsisEntry& entry) {
                            return entry.disposition == ir::Disposition::Routed;
                                   }) |
                                   std::ranges::to<std::vector>();
    REQUIRE(routed.size() == 4);
    // Both fields name the class: `name` because the wording is the class's
    // own, `parent` because a gathered synopsis names none of the classes it
    // folded in (issue #45) and the entry is then the only thing that says
    // which one a finding is about.
    CHECK(std::ranges::all_of(routed, [](const ir::SynopsisEntry& entry) { return entry.name == entry.parent; }));

    const auto section_of = [&routed](std::string_view name) {
        const auto found = std::ranges::find(routed, name, &ir::SynopsisEntry::name);
        return found == routed.end() ? std::string{"<none>"} : found->section;
    };
    CHECK(section_of("crate") == "box.crate");
    CHECK(section_of("pallet") == "box.pallet");
    CHECK(section_of("label") == "box.label");
    // The dangling one: the section it names is opened by no `\rSec`, which
    // is exactly what the validator reads this entry to report.
    CHECK(section_of("strap") == "box.nosuch");
    CHECK(section_of("crumb") == "<none>");
}
