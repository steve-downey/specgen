// tests/beman/specgen/frontend/leading_description.test.cpp        -*-C++-*-
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
const ir::Section* section(const std::vector<ir::Node>& nodes, const std::string& stable);
const ir::Section* section(const std::vector<ir::Node>& nodes, const std::string& stable) {
    for (const auto& node : nodes) {
        if (const auto* value = std::get_if<ir::Section>(&node)) {
            if (value->stable_name == stable)
                return value;
            if (const auto* nested = section(value->children, stable))
                return nested;
        }
    }
    return nullptr;
}
} // namespace

TEST_CASE("leading descriptions route classes and documented declaration-only members") {
    const auto built =
        frontend::build_document(std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_leading_description.hpp");
    REQUIRE(built.has_value());
    CHECK(built->diagnostics.empty());
    const auto* general = section(built->document.nodes, "leading.general");
    REQUIRE(general != nullptr);
    REQUIRE(general->children.size() == 1);
    const auto& own = std::get<ir::SpecItem>(general->children.front());
    CHECK(own.decl.signatures.empty());
    CHECK(own.descr.leading.size() == 2);
    const auto* members = section(built->document.nodes, "leading.members");
    REQUIRE(members != nullptr);
    REQUIRE(members->children.size() == 2);
    for (const auto& node : members->children)
        CHECK(ir::has_description(std::get<ir::SpecItem>(node).descr));
    const auto* functions = section(built->document.nodes, "leading.functions");
    REQUIRE(functions != nullptr);
    REQUIRE(functions->children.size() == 3);
    CHECK(std::get<ir::SpecItem>(functions->children[0]).decl.signatures.size() == 1);
    CHECK(std::get<ir::SpecItem>(functions->children[1]).decl.signatures.size() == 1);
    const auto& grouped = std::get<ir::SpecItem>(functions->children[2]);
    CHECK(grouped.decl.signatures.size() == 2);
    REQUIRE(grouped.descr.leading.size() == 4);
    REQUIRE(std::holds_alternative<ir::Itemize>(grouped.descr.leading[1]));
    CHECK(std::get<ir::Itemize>(grouped.descr.leading[1]).items.size() == 2);
    CHECK(std::get<ir::BodyParagraph>(grouped.descr.leading[2]).continuation);
    CHECK(grouped.descr.elements.size() == 1);
}

TEST_CASE("leading prose participates in leakage validation") {
    const auto built = frontend::build_document(std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_leading_leak.hpp");
    REQUIRE(built.has_value());
    const auto findings = beman::specgen::validate::validate(built->document);
    CHECK(beman::specgen::validate::has_errors(findings));
    CHECK(std::ranges::any_of(findings, [](const auto& finding) {
        return finding.context.contains("description") && finding.message.contains("implementation");
    }));
}
