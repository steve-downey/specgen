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
    CHECK(synopsis.roster.empty());
    const ir::CodeText& code = synopsis.code;
    CHECK(code.text.contains("struct tag"));
    CHECK(code.text.contains("inline constexpr tag value"));
    CHECK_FALSE(code.text.contains("omitted_helper"));
    CHECK_FALSE(code.text.contains("merged_helper"));
    CHECK(code.text.contains("class widget;"));
    CHECK(code.text.contains("void swap(widget<T>&, widget<T>&);"));
    CHECK(code.text.contains("bool operator==(const widget<T>&, const widget<T>&);"));
    CHECK_FALSE(code.text.contains("operator===="));
    CHECK(code.text.contains("namespace std {\n  template<class T> struct hash<demo::widget<T>>;\n}"));
    CHECK_FALSE(code.text.contains("API documentation"));
    CHECK_FALSE(code.text.contains("verbatim-synopsis"));
    CHECK_FALSE(code.text.contains("END [widget.syn]"));

    const auto spans = [&](ir::SpanKind kind) {
        return code.spans | std::views::filter([=](const ir::Span& span) { return span.kind == kind; }) |
               std::ranges::to<std::vector>();
    };
    const auto refs = spans(ir::SpanKind::Ref);
    REQUIRE(refs.size() == 1);
    CHECK(refs.front().payload == "widget.ops");
    CHECK(code.text.substr(refs.front().begin, refs.front().end - refs.front().begin) == "[widget.ops]");

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
