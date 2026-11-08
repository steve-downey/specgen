// tests/beman/specgen/frontend/expos_static_items.test.cpp          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/build_document.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/validate/validate.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

namespace frontend = beman::specgen::frontend;
namespace ir       = beman::specgen::ir;

namespace {
void collect_items(const std::vector<ir::Node>& nodes, std::vector<const ir::SpecItem*>& items);
void collect_items(const std::vector<ir::Node>& nodes, std::vector<const ir::SpecItem*>& items) {
    for (const ir::Node& node : nodes) {
        if (const auto* section = std::get_if<ir::Section>(&node))
            collect_items(section->children, items);
        if (const auto* item = std::get_if<ir::SpecItem>(&node))
            items.push_back(item);
    }
}
} // namespace

TEST_CASE("exposition static descriptions route with semantic names and no normative index") {
    const auto built =
        frontend::build_document(std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_expos_static_items.hpp");
    REQUIRE(built.has_value());
    CHECK(built->diagnostics.empty());
    std::vector<const ir::SpecItem*> items;
    collect_items(built->document.nodes, items);
    REQUIRE(items.size() == 3);
    for (const auto* item : items) {
        CHECK(item->decl.index.empty());
        REQUIRE(item->decl.entities.size() == 1);
        CHECK(item->decl.entities.front() != "marker_only_");
        REQUIRE(item->decl.signatures.size() == 1);
        CHECK(std::ranges::any_of(item->decl.signatures.front().spans,
                                  [](const ir::Span& span) { return span.kind == ir::SpanKind::ExposId; }));
    }
    const auto templated =
        std::ranges::find_if(items, [](const auto* item) { return item->decl.entities.front() == "accepts_"; });
    REQUIRE(templated != items.end());
    CHECK((*templated)->decl.signatures.front().text.contains("template<class T>"));
    CHECK_FALSE((*templated)->decl.signatures.front().text.contains("sizeof"));
    CHECK(std::ranges::any_of((*templated)->decl.signatures.front().spans,
                              [](const ir::Span& span) { return span.kind == ir::SpanKind::SeeBelow; }));
}

TEST_CASE("exposition static missing routes remain visible and non-static wording is rejected") {
    const auto built =
        frontend::build_document(std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_expos_static_missing.hpp");
    REQUIRE(built.has_value());
    CHECK(std::ranges::any_of(built->diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.contains("documented data member produces no wording");
    }));
    const auto diagnostics = beman::specgen::validate::validate(built->document);
    CHECK(std::ranges::any_of(diagnostics,
                              [](const auto& diagnostic) { return diagnostic.message.contains("nowhere"); }));
}
