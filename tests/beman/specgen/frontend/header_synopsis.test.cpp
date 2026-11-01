// tests/beman/specgen/frontend/header_synopsis.test.cpp         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/header_synopsis.hpp>
#include <beman/specgen/frontend/header_synopsis.hpp> // Re-inclusion verification

#include <beman/specgen/frontend/build_document.hpp>
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
    // The region is enclosed in `namespace std` once, as a whole, so a
    // verbatim payload inside it is written as the draft writes it there.
    CHECK(code.text.starts_with("namespace std {\n  struct tag {};\n"));
    CHECK(code.text.ends_with("\n  template<class T> struct hash<widget<T>>;\n}"));
    CHECK_FALSE(code.text.contains("namespace std {\n  namespace std"));
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

// The enclosure takes whatever namespace it is handed (issue #209): `std`
// for the common case, `std::proto` for a class in a specified nested
// namespace, `views` for a run inside a gathered region. Spans move with
// their text either way.
TEST_CASE("header_synopsis - enclose_in_namespace writes the name given and moves spans with the text") {
    ir::CodeText code{"int x;\nint y;", {ir::Span{4, 5, ir::SpanKind::LibraryIndex, "x"}}};
    frontend::enclose_in_namespace(code, "std::proto");
    CHECK(code.text == "namespace std::proto {\n  int x;\n  int y;\n}");
    REQUIRE(code.spans.size() == 1);
    CHECK(code.text.substr(code.spans.front().begin, code.spans.front().end - code.spans.front().begin) == "x");
}

TEST_CASE("header_synopsis - enclosures compose into nested, doubly indented blocks") {
    ir::CodeText inner{"int b;", {}};
    frontend::enclose_in_namespace(inner, "views");
    ir::CodeText outer{"int a;", {}};
    frontend::append_synopsis_code(outer, std::move(inner));
    frontend::enclose_in_namespace(outer, "std");
    CHECK(outer.text == "namespace std {\n  int a;\n\n  namespace views {\n    int b;\n  }\n}");
}

// The gathered region's runs (issue #209): a change of nested namespace
// closes the open block and opens the next, in source order, the way
// [time.syn] re-opens `namespace chrono`; a group header comment is deferred
// to the declaration it heads, so it lands inside that declaration's block.
TEST_CASE("header_synopsis - NamespaceRuns opens a block per run and defers comments to the next declaration") {
    frontend::NamespaceRuns runs;
    runs.append(ir::CodeText{"int a;", {}}, "");
    runs.append_comment(ir::CodeText{"// [x], group", {}});
    runs.append(ir::CodeText{"int b;", {}}, "views");
    runs.append(ir::CodeText{"int c;", {}}, "views");
    runs.append(ir::CodeText{"int d;", {}}, "proto");
    runs.append(ir::CodeText{"int e;", {}}, "");
    runs.append_comment(ir::CodeText{"// tail", {}});
    const ir::CodeText code = runs.finish();
    CHECK(code.text == "int a;\n\n"
                       "namespace views {\n  // [x], group\n\n  int b;\n\n  int c;\n}\n\n"
                       "namespace proto {\n  int d;\n}\n\n"
                       "int e;\n\n"
                       "// tail");
}

TEST_CASE("header_synopsis - NamespaceRuns with no nested namespace is the plain concatenation") {
    frontend::NamespaceRuns runs;
    runs.append(ir::CodeText{"int a;", {}}, "");
    runs.append_comment(ir::CodeText{"// [x], group", {}});
    runs.append(ir::CodeText{"int b;", {}}, "");
    CHECK(runs.finish().text == "int a;\n\n// [x], group\n\nint b;");
}

// End to end on the corpus header written for it: the blocks the region
// opens, the enclosure a nested class takes, and the declarators the
// itemdecls qualify (issue #209).
TEST_CASE("build_document renders specified nested namespaces as blocks, enclosures, and qualifiers") {
    const auto built = frontend::build_document(kCorpus + "/spec_nested_namespace.hpp");
    REQUIRE(built.has_value());

    const ir::Section* syn = find_section(built->document.nodes, "demo.syn");
    REQUIRE(syn != nullptr);
    const auto synopses =
        syn->children |
        std::views::filter([](const ir::Node& node) { return std::holds_alternative<ir::Synopsis>(node); }) |
        std::ranges::to<std::vector>();
    REQUIRE(synopses.size() == 1);
    const std::string& code = std::get<ir::Synopsis>(synopses.front()).code.text;
    CHECK(code.starts_with("namespace std {\n"));
    CHECK(code.contains("\n  namespace views {\n"));
    CHECK(code.contains("\n  namespace proto {\n"));
    CHECK(code.contains("\n  namespace ranges {\n"));
    // The unmarked implementation namespace flattens as it always did.
    CHECK_FALSE(code.contains("namespace detail"));
    // A `views::` qualifier written in code survives, fully qualified or not.
    CHECK(code.contains("sizeof(views::adapt)"));
    CHECK_FALSE(code.contains("demo::"));
    // The group header above a nested declaration lands in its block.
    CHECK(code.contains("  namespace views {\n    // [demo.views], range adaptor objects\n"));
    // The two `decode`s are in different blocks, not siblings.
    CHECK(code.contains("    template<encoding E> decoder<E> decode(const char* text);\n  }\n"));
    CHECK(code.contains("\n  template<class D> D decode(D d);\n"));

    const ir::Section* decoder = find_section(built->document.nodes, "demo.decoder");
    REQUIRE(decoder != nullptr);
    const auto class_synopses =
        decoder->children |
        std::views::filter([](const ir::Node& node) { return std::holds_alternative<ir::Synopsis>(node); }) |
        std::ranges::to<std::vector>();
    REQUIRE(class_synopses.size() == 1);
    CHECK(std::get<ir::Synopsis>(class_synopses.front()).code.text.starts_with("namespace std::proto {\n"));

    const auto signatures_in = [&](std::string_view stable) {
        const ir::Section* section = find_section(built->document.nodes, stable);
        REQUIRE(section != nullptr);
        return section->children |
               std::views::filter([](const ir::Node& node) { return std::holds_alternative<ir::SpecItem>(node); }) |
               std::views::transform(
                   [](const ir::Node& node) { return std::get<ir::SpecItem>(node).decl.signatures.front().text; }) |
               std::ranges::to<std::vector<std::string>>();
    };
    const std::vector<std::string> decode = signatures_in("demo.decode");
    REQUIRE(decode.size() == 2);
    CHECK(decode[0].contains(" proto::decode(const char* text);"));
    CHECK(decode[1] == "template<class D> D decode(D d);");
    const std::vector<std::string> adapt = signatures_in("demo.views");
    REQUIRE(adapt.size() == 1);
    CHECK(adapt.front().contains(" views::adapt;"));
    // A type stays bare: the grammar has no qualified enum-head to write.
    const std::vector<std::string> encoding = signatures_in("demo.encoding");
    REQUIRE(encoding.size() == 1);
    CHECK(encoding.front().starts_with("enum class encoding"));
}
