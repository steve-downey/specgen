// tests/beman/specgen/frontend/aliases.test.cpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/frontend.hpp>
#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <variant>

namespace frontend = beman::specgen::frontend;
namespace ir       = beman::specgen::ir;

namespace {
const std::string kCorpusHeader      = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_aliases.hpp";
const std::string kDiagnosticsHeader = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_alias_diagnostics.hpp";
} // namespace

TEST_CASE("build_document - alias-only markers diagnose other entity shapes") {
    const auto built = frontend::build_document(kDiagnosticsHeader);
    REQUIRE(built.has_value());
    CHECK(std::ranges::any_of(built->diagnostics, [](const auto& diagnostic) {
        return diagnostic.message == "\\impdef applies only to type aliases";
    }));
    CHECK(std::ranges::any_of(built->diagnostics, [](const auto& diagnostic) {
        return diagnostic.message == "a type alias accepts only bare \\seebelow";
    }));
}

TEST_CASE("build_document - documented aliases route, group, and mask their RHS") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());

    const auto synopsis = std::ranges::find_if(built->document.nodes, [](const ir::Node& node) {
        const auto* value = std::get_if<ir::Synopsis>(&node);
        return value != nullptr && value->name == "range";
    });
    REQUIRE(synopsis != built->document.nodes.end());
    const auto& syn = std::get<ir::Synopsis>(*synopsis);
    CHECK(syn.code.text.find("using iterator = implementation-defined;") != std::string::npos);
    CHECK(syn.code.text.find("using token_type = SEEBELOW;") != std::string::npos);
    CHECK(std::ranges::count_if(
              syn.roster, [](const ir::SynopsisEntry& entry) { return entry.kind == ir::MemberKind::Alias; }) == 4);
    CHECK(std::ranges::none_of(syn.roster,
                               [](const ir::SynopsisEntry& entry) { return entry.name == "unmarked_type"; }));
    for (const std::string_view name : {"value_type", "const_value_type"}) {
        const auto entry = std::ranges::find(syn.roster, name, &ir::SynopsisEntry::name);
        REQUIRE(entry != syn.roster.end());
        CHECK(entry->disposition == ir::Disposition::Routed);
        CHECK(entry->section == "range.types");
    }

    const auto section = std::ranges::find_if(built->document.nodes, [](const ir::Node& node) {
        const auto* value = std::get_if<ir::Section>(&node);
        return value != nullptr && value->stable_name == "range.types";
    });
    REQUIRE(section != built->document.nodes.end());
    const auto& types = std::get<ir::Section>(*section);
    REQUIRE(types.children.size() == 3);

    const auto& iterator = std::get<ir::SpecItem>(types.children[0]);
    REQUIRE(iterator.decl.signatures[0].spans.size() == 1);
    CHECK(iterator.decl.signatures[0].spans[0].kind == ir::SpanKind::ImplDefined);

    const auto& values = std::get<ir::SpecItem>(types.children[1]);
    REQUIRE(values.decl.signatures.size() == 2);
    CHECK(values.decl.signatures[0].text.find("using value_type = T;") != std::string::npos);
    CHECK(values.decl.signatures[1].text.find("using const_value_type = const T;") != std::string::npos);
    REQUIRE(values.decl.index.size() == 2);
    CHECK(values.decl.index[0].name == "value_type");
    CHECK(values.decl.index[1].name == "const_value_type");

    const auto& token = std::get<ir::SpecItem>(types.children[2]);
    REQUIRE(token.decl.signatures[0].spans.size() == 1);
    CHECK(token.decl.signatures[0].spans[0].kind == ir::SpanKind::SeeBelow);
}
