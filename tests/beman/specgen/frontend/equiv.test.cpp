// tests/beman/specgen/frontend/equiv.test.cpp                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/frontend.hpp>
#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>

namespace frontend = beman::specgen::frontend;
namespace ir       = beman::specgen::ir;

namespace {
const std::string kCorpusHeader = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_equiv.hpp";
}

TEST_CASE("build_document - Mandates prologue retains aliases and consumes only its assertions") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());

    const ir::Section* observe = nullptr;
    for (const ir::Node& node : built->document.nodes) {
        const auto* section = std::get_if<ir::Section>(&node);
        if (section != nullptr && section->stable_name == "box.observe")
            observe = section;
    }
    REQUIRE(observe != nullptr);
    REQUIRE(observe->children.size() == 2);

    const auto& reset = std::get<ir::SpecItem>(observe->children[1]);
    REQUIRE(reset.descr.elements.size() == 2);
    const auto& mandates = reset.descr.elements[0];
    REQUIRE(mandates.conjuncts.size() == 1);
    CHECK(std::get<ir::CodeInline>(mandates.conjuncts[0][0]).code.text == "is_trivial_v<T>");

    REQUIRE(reset.descr.elements[1].equivalent.has_value());
    const std::string& code = reset.descr.elements[1].equivalent->code.text;
    CHECK(code.starts_with("using value_type = T;"));
    CHECK_FALSE(code.contains("static_assert(is_trivial_v<T>)"));
    CHECK(code.contains("static_assert(sizeof(value_type) > 0);"));
    CHECK(code.contains("engaged = false;"));
}

TEST_CASE("build_document - Equivalent-to extraction strips only documentation comments") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());

    const ir::Section* observe = nullptr;
    for (const ir::Node& node : built->document.nodes) {
        const auto* section = std::get_if<ir::Section>(&node);
        if (section != nullptr && section->stable_name == "box.observe")
            observe = section;
    }
    REQUIRE(observe != nullptr);
    REQUIRE(observe->children.size() == 2);

    const auto& reset = std::get<ir::SpecItem>(observe->children[1]);
    REQUIRE(reset.descr.elements[1].equivalent.has_value());
    const std::string& code = reset.descr.elements[1].equivalent->code.text;
    CHECK_FALSE(code.contains("Whole-line Doxygen"));
    CHECK_FALSE(code.contains("Whole-line specgen"));
    CHECK_FALSE(code.contains("Trailing Doxygen"));
    CHECK_FALSE(code.contains("Trailing specgen"));
    CHECK(code.contains("value = T();\nstatic_assert"));
    CHECK(code.contains("// A draft-form body comment survives."));
    CHECK(code.contains("/* A draft-form block comment survives. */"));
    CHECK(code.contains("\"/// string content survives\""));
    CHECK(code.contains("\"/** string content survives */\""));
    CHECK(code.contains("R\"body(//! raw string content survives\n/** raw string content survives */)body\""));
    CHECK(code.ends_with("engaged = false;"));
}
