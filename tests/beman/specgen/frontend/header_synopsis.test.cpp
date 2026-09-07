// tests/beman/specgen/frontend/header_synopsis.test.cpp         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/frontend.hpp>
#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>
#include <string>
#include <variant>

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
} // namespace

TEST_CASE("build_document gathers a bounded header synopsis into one node") {
    const auto built = frontend::build_document(kCorpus + "/spec_header_synopsis.hpp");
    REQUIRE(built.has_value());

    const ir::Section* section = find_section(built->document.nodes, "widget.syn");
    REQUIRE(section != nullptr);
    const auto synopses =
        section->children |
        std::views::filter([](const ir::Node& node) { return std::holds_alternative<ir::Synopsis>(node); }) |
        std::ranges::to<std::vector>();
    REQUIRE(synopses.size() == 1);

    const ir::Synopsis& synopsis = std::get<ir::Synopsis>(synopses.front());
    // The gathered node's roster is the folded-in classes' own, each entry
    // naming the class that declared it, since the node names none of them
    // (issue #45). Only `sentinel` is a class *definition* in this region --
    // the free functions and the forward-declared `widget` have no class body
    // and so no roster of their own. The region's own routed namespace
    // entities are on it too, and name no class, which is what an empty
    // `parent` means.
    CHECK_FALSE(synopsis.roster.empty());
    CHECK(std::ranges::all_of(synopsis.roster, [](const ir::SynopsisEntry& entry) {
        return entry.parent == "sentinel" || entry.parent.empty();
    }));
    CHECK(std::ranges::any_of(synopsis.roster,
                              [](const ir::SynopsisEntry& entry) { return entry.parent == "sentinel"; }));
    CHECK(std::ranges::any_of(synopsis.roster, [](const ir::SynopsisEntry& entry) {
        return entry.name == "operator==" && entry.disposition == ir::Disposition::Routed;
    }));
    const ir::CodeText& code = synopsis.code;
    CHECK(code.text.contains("struct tag"));
    CHECK(code.text.contains("inline constexpr tag value"));
    CHECK_FALSE(code.text.contains("omitted_helper"));
    CHECK_FALSE(code.text.contains("merged_helper"));
    CHECK(code.text.contains("class widget;"));
    CHECK(code.text.contains("void swap(widget<T>&, widget<T>&);"));
    CHECK(code.text.contains("bool operator==(const widget<T>&, const widget<T>&);"));
    CHECK(code.text.contains("struct sentinel {"));
    CHECK_FALSE(code.text.contains("operator===="));
    CHECK(code.text.contains("namespace std {\n  template<class T> struct hash<demo::widget<T>>;\n}"));
    CHECK_FALSE(code.text.contains("API documentation"));
    CHECK_FALSE(code.text.contains("verbatim-synopsis"));
    CHECK_FALSE(code.text.contains("END [widget.syn]"));

    const auto spans = [&](ir::SpanKind kind) {
        return code.spans | std::views::filter([=](const ir::Span& span) { return span.kind == kind; }) |
               std::ranges::to<std::vector>();
    };
    // Three `\ref` group headers now: the free-function one, the one inside
    // the class defined in the region, which the class extraction carries, and
    // the one routing the customization point objects that follow it.
    const auto refs = spans(ir::SpanKind::Ref);
    REQUIRE(refs.size() == 3);
    CHECK(std::ranges::all_of(refs, [&](const ir::Span& span) {
        return code.text.substr(span.begin, span.end - span.begin) == "[" + span.payload + "]";
    }));
    CHECK(std::ranges::count_if(refs, [](const ir::Span& span) { return span.payload == "widget.ops"; }) == 2);
    CHECK(std::ranges::count_if(refs, [](const ir::Span& span) { return span.payload == "widget.cpo"; }) == 1);

    const auto expos = spans(ir::SpanKind::ExposId);
    REQUIRE(expos.size() == 1);
    CHECK(expos.front().payload == "widget-like");
    CHECK(code.text.substr(expos.front().begin, expos.front().end - expos.front().begin) == "widget-like");

    const auto indexes = spans(ir::SpanKind::LibraryIndex);
    REQUIRE(indexes.size() >= 5);
    CHECK(std::ranges::any_of(indexes, [&](const ir::Span& span) {
        return code.text.substr(span.begin, span.end - span.begin) == "widget";
    }));
}

// A class defined inside the gathered region routes its in-class members the
// same way a class outside it does: the fold owns the only event they can
// travel on, and taking the class's code alone dropped them (issue #34) --
// declaration in the synopsis, description nowhere, target section empty.
TEST_CASE("a gathered region's class routes its in-class members to their sections") {
    const auto built = frontend::build_document(kCorpus + "/spec_header_synopsis.hpp");
    REQUIRE(built.has_value());

    const ir::Section* ops = find_section(built->document.nodes, "widget.ops");
    REQUIRE(ops != nullptr);
    const auto items =
        ops->children |
        std::views::filter([](const ir::Node& node) { return std::holds_alternative<ir::SpecItem>(node); }) |
        std::ranges::to<std::vector>();
    REQUIRE(items.size() == 1);

    const ir::SpecItem& item = std::get<ir::SpecItem>(items.front());
    REQUIRE(item.decl.signatures.size() == 1);
    CHECK(item.decl.signatures.front().text.contains("operator==(const T& t, sentinel)"));
    REQUIRE(item.descr.elements.size() == 1);
    CHECK(item.descr.elements.front().kind == ir::ElementKind::Returns);
}

// The other half of what a folded-in class carries (issue #41): its
// class-general paragraph and its own description are not routed anywhere, so
// they belong beside its synopsis -- which, inside a region, means in the
// gathered section. The fold used to keep only the class's code and let the
// event go, taking both with it.
TEST_CASE("a gathered region's class keeps its general paragraph and its own description") {
    const auto built = frontend::build_document(kCorpus + "/spec_header_synopsis.hpp");
    REQUIRE(built.has_value());

    const ir::Section* syn = find_section(built->document.nodes, "widget.syn");
    REQUIRE(syn != nullptr);

    const auto paragraphs =
        syn->children |
        std::views::filter([](const ir::Node& node) { return std::holds_alternative<ir::FreeParagraph>(node); }) |
        std::ranges::to<std::vector>();
    REQUIRE(paragraphs.size() == 1);

    // The class's own description is the description-only item here: an
    // unrouted namespace entity's wording rides out beside the synopsis too
    // (issue #69), and that one carries its declaration.
    const auto items =
        syn->children |
        std::views::filter([](const ir::Node& node) { return std::holds_alternative<ir::SpecItem>(node); }) |
        std::views::transform([](const ir::Node& node) { return std::get<ir::SpecItem>(node); }) |
        std::ranges::to<std::vector>();
    REQUIRE(items.size() == 2);
    const auto described = items |
                           std::views::filter([](const ir::SpecItem& item) { return item.decl.signatures.empty(); }) |
                           std::ranges::to<std::vector>();
    REQUIRE(described.size() == 1);
    REQUIRE(described.front().descr.elements.size() == 1);
    CHECK(described.front().descr.elements.front().kind == ir::ElementKind::Remarks);
}

// A namespace entity folded into the region keeps its wording, which the fold
// used to drop (issue #69). Unrouted, it rides out beside the synopsis, in the
// section the region itself is in -- declaration and all, since a variable's
// declaration is its itemdecl.
TEST_CASE("a gathered region's unrouted namespace entity keeps its wording") {
    const auto built = frontend::build_document(kCorpus + "/spec_header_synopsis.hpp");
    REQUIRE(built.has_value());

    const ir::Section* syn = find_section(built->document.nodes, "widget.syn");
    REQUIRE(syn != nullptr);
    const auto items =
        syn->children |
        std::views::filter([](const ir::Node& node) { return std::holds_alternative<ir::SpecItem>(node); }) |
        std::views::transform([](const ir::Node& node) { return std::get<ir::SpecItem>(node); }) |
        std::views::filter([](const ir::SpecItem& item) { return !item.decl.signatures.empty(); }) |
        std::ranges::to<std::vector>();
    REQUIRE(items.size() == 1);
    CHECK(items.front().decl.signatures.front().text.contains("limit"));
    REQUIRE(items.front().descr.elements.size() == 1);
    CHECK(items.front().descr.elements.front().kind == ir::ElementKind::Remarks);
}

// And routed, it goes where it is sent: `\at` overrides the `\ref` group
// header in force, and a `\ref` header inside the region routes what follows
// it, exactly as one inside a class body does. A range adaptor object needs
// this and has no alternative -- it has no out-of-line definition to carry a
// description into a later section, and its declaration belongs in the header
// synopsis.
TEST_CASE("a gathered region routes a namespace entity's wording to its section") {
    const auto built = frontend::build_document(kCorpus + "/spec_header_synopsis.hpp");
    REQUIRE(built.has_value());

    const ir::Section* cpo = find_section(built->document.nodes, "widget.cpo");
    REQUIRE(cpo != nullptr);
    const auto items =
        cpo->children |
        std::views::filter([](const ir::Node& node) { return std::holds_alternative<ir::SpecItem>(node); }) |
        std::views::transform([](const ir::Node& node) { return std::get<ir::SpecItem>(node); }) |
        std::ranges::to<std::vector>();
    REQUIRE(items.size() == 2);

    // \at first, in source order: it sits above the `\ref` header, under
    // `\ref{widget.ops}`, and lands here anyway.
    REQUIRE(items[0].decl.signatures.size() == 1);
    CHECK(items[0].decl.signatures.front().text.contains("tag_of"));
    REQUIRE(items[0].descr.elements.size() == 1);
    CHECK(items[0].descr.elements.front().kind == ir::ElementKind::Effects);

    REQUIRE(items[1].decl.signatures.size() == 1);
    CHECK(items[1].decl.signatures.front().text.contains("make_widget"));

    // The declarations are still in the synopsis: routing moves the wording,
    // not the declaration.
    const ir::Section* syn = find_section(built->document.nodes, "widget.syn");
    REQUIRE(syn != nullptr);
    const auto synopses =
        syn->children |
        std::views::filter([](const ir::Node& node) { return std::holds_alternative<ir::Synopsis>(node); }) |
        std::ranges::to<std::vector>();
    REQUIRE(synopses.size() == 1);
    const std::string& code = std::get<ir::Synopsis>(synopses.front()).code.text;
    CHECK(code.contains("tag_of"));
    CHECK(code.contains("make_widget"));
}

TEST_CASE("malformed header synopsis boundaries do not swallow later sections") {
    const auto built = frontend::build_document(kCorpus + "/spec_header_synopsis_invalid.hpp");
    REQUIRE(built.has_value());

    REQUIRE(find_section(built->document.nodes, "after.mismatch") != nullptr);
    REQUIRE(find_section(built->document.nodes, "after.nested") != nullptr);
    CHECK(std::ranges::any_of(built->diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.contains("mismatched END [different.syn]");
    }));
    CHECK(std::ranges::any_of(built->diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.contains("not closed before section [after.nested]");
    }));
}

// A routed namespace entity earns a roster entry, which is the only record
// that the route was asked for: build_tree drops a pending item whose section
// no `\rSec` opens, so without the entry §9's dangling-route rule has nothing
// to read and a typo loses the wording silently.
TEST_CASE("a gathered region's routed namespace entity is on the roster") {
    const auto built = frontend::build_document(kCorpus + "/spec_header_synopsis.hpp");
    REQUIRE(built.has_value());

    const ir::Section* syn = find_section(built->document.nodes, "widget.syn");
    REQUIRE(syn != nullptr);
    const auto synopses =
        syn->children |
        std::views::filter([](const ir::Node& node) { return std::holds_alternative<ir::Synopsis>(node); }) |
        std::ranges::to<std::vector>();
    REQUIRE(synopses.size() == 1);

    const auto routed = std::get<ir::Synopsis>(synopses.front()).roster |
                        std::views::filter([](const ir::SynopsisEntry& entry) {
                            return entry.disposition == ir::Disposition::Routed &&
                                   (entry.name == "tag_of" || entry.name == "make_widget");
                        }) |
                        std::ranges::to<std::vector>();
    REQUIRE(routed.size() == 2);
    CHECK(std::ranges::all_of(routed, [](const ir::SynopsisEntry& entry) {
        return entry.section == "widget.cpo" && entry.kind == ir::MemberKind::Data && entry.parent.empty();
    }));

    // The unrouted one is not on it: its wording is in the document, beside
    // the synopsis, so there is no route to check.
    CHECK(std::ranges::none_of(std::get<ir::Synopsis>(synopses.front()).roster,
                               [](const ir::SynopsisEntry& entry) { return entry.name == "limit"; }));
}
