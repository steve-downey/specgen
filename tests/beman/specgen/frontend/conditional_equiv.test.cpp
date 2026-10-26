// tests/beman/specgen/frontend/conditional_equiv.test.cpp         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/build_document.hpp>
#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>

namespace frontend = beman::specgen::frontend;
namespace ir       = beman::specgen::ir;

namespace {
const std::string kCorpusHeader = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_conditional_equiv.hpp";

const ir::SpecItem* item_with(const ir::Document& document, std::string_view signature) {
    for (const ir::Node& node : document.nodes) {
        const auto* section = std::get_if<ir::Section>(&node);
        if (section == nullptr)
            continue;
        for (const ir::Node& child : section->children) {
            const auto* item = std::get_if<ir::SpecItem>(&child);
            if (item != nullptr && !item->decl.signatures.empty() && item->decl.signatures[0].text.contains(signature))
                return item;
        }
    }
    return nullptr;
}

// The code of a \returns-expr item's *Returns*: `E`, the paragraph's first
// inline.
const std::string& returns_code(const ir::SpecItem& item) {
    REQUIRE(item.descr.elements.size() == 1);
    REQUIRE(item.descr.elements[0].kind == ir::ElementKind::Returns);
    REQUIRE_FALSE(item.descr.elements[0].equivalent.has_value());
    REQUIRE(item.descr.elements[0].paragraphs.size() == 1);
    const auto* code = std::get_if<ir::CodeInline>(&item.descr.elements[0].paragraphs[0].at(0));
    REQUIRE(code != nullptr);
    return code->code.text;
}

const std::string& equivalent_code(const ir::SpecItem& item) {
    REQUIRE(item.descr.elements.size() == 1);
    REQUIRE(item.descr.elements[0].equivalent.has_value());
    return item.descr.elements[0].equivalent->code.text;
}
} // namespace

TEST_CASE("build_document - equivalent bodies contain only the selected conditional branches") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());

    const ir::SpecItem* select = item_with(built->document, "select()");
    REQUIRE(select != nullptr);
    // The returned expression comes from the one return statement the
    // selected branch leaves in the AST.
    CHECK(returns_code(*select) == "1");

    const ir::SpecItem* assign = item_with(built->document, "assign()");
    REQUIRE(assign != nullptr);
    const std::string& selected_assignment = equivalent_code(*assign);
    CHECK(selected_assignment.contains("value = 1; // selected assignment"));
    CHECK(selected_assignment.contains("(void)value;"));
    CHECK_FALSE(selected_assignment.contains("value = 99"));
    CHECK_FALSE(selected_assignment.contains("value = 88"));
    CHECK_FALSE(selected_assignment.contains("value = 2"));
    CHECK_FALSE(selected_assignment.contains('#'));
}
