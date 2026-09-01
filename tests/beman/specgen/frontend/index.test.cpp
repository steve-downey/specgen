// tests/beman/specgen/frontend/index.test.cpp                     -*-C++-*-
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
const std::string kCorpusHeader = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_index.hpp";
}

TEST_CASE("build_document - derives item and synopsis library indexes") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());

    const auto synopsis = std::ranges::find_if(built->document.nodes, [](const ir::Node& node) {
        const auto* value = std::get_if<ir::Synopsis>(&node);
        return value != nullptr && value->name == "indexed";
    });
    REQUIRE(synopsis != built->document.nodes.end());
    const ir::CodeText& code = std::get<ir::Synopsis>(*synopsis).code;
    const auto          library_spans =
        code.spans | std::views::filter([](const ir::Span& span) { return span.kind == ir::SpanKind::LibraryIndex; }) |
        std::ranges::to<std::vector>();
    REQUIRE(library_spans.size() == 2);
    CHECK(library_spans[0].payload.empty());
    CHECK(code.text.substr(library_spans[0].begin, library_spans[0].end - library_spans[0].begin) == "indexed");
    CHECK(library_spans[1].payload == "indexed");
    CHECK(code.text.substr(library_spans[1].begin, library_spans[1].end - library_spans[1].begin) == "value_type");

    std::vector<ir::IndexEntry> indexes;
    for (const ir::Node& node : built->document.nodes) {
        const auto* section = std::get_if<ir::Section>(&node);
        if (section == nullptr)
            continue;
        for (const ir::Node& child : section->children) {
            if (const auto* item = std::get_if<ir::SpecItem>(&child))
                indexes.append_range(item->decl.index);
        }
    }

    REQUIRE(indexes.size() == 4);
    CHECK(indexes[0].kind == ir::IndexKind::Constructor);
    CHECK(indexes[0].name == "indexed");
    CHECK(indexes[0].parent.empty());
    CHECK(indexes[1].kind == ir::IndexKind::Destructor);
    CHECK(indexes[1].name == "indexed");
    CHECK(indexes[1].parent.empty());
    CHECK(indexes[2].kind == ir::IndexKind::Member);
    CHECK(indexes[2].name == "value");
    CHECK(indexes[2].parent == "indexed");
    CHECK(indexes[3].kind == ir::IndexKind::Global);
    CHECK(indexes[3].name == "operator==");
    CHECK(indexes[3].parent.empty());
}
