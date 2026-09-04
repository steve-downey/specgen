// tests/beman/specgen/frontend/class_description.test.cpp          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// build_document() over the hand-curated corpus header
// (tests/corpus/spec_class_description.hpp): a class or class-template
// *definition*'s own docblock describes the type (design §6, issue #18).
// The elements land in a description-only SpecItem -- one carrying no
// signatures -- immediately after the class's synopsis, and an authored
// `\mandates` replaces the class-scope derived paragraph of design §5.2
// while inheriting its conjuncts as validator-only drift evidence.

#include <beman/specgen/frontend/frontend.hpp>
#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace frontend = beman::specgen::frontend;
namespace ir       = beman::specgen::ir;

namespace {

const std::string kCorpusHeader = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_class_description.hpp";

std::string paragraph_text(const ir::Paragraph& para) {
    std::string out;
    for (const ir::Inline& piece : para) {
        if (const auto* text = std::get_if<ir::TextInline>(&piece))
            out += text->text;
        else if (const auto* code = std::get_if<ir::CodeInline>(&piece))
            out += code->code.text;
    }
    return out;
}

// The description-only item that follows the synopsis named @p class_name:
// the next node after it that is a SpecItem with no signatures.
const ir::SpecItem* class_description(const ir::Document& document, std::string_view class_name) {
    bool seen = false;
    for (const ir::Node& node : document.nodes) {
        if (const auto* synopsis = std::get_if<ir::Synopsis>(&node)) {
            seen = synopsis->name == class_name;
            continue;
        }
        const auto* item = std::get_if<ir::SpecItem>(&node);
        if (seen && item != nullptr && item->decl.signatures.empty())
            return item;
    }
    return nullptr;
}

} // namespace

TEST_CASE("build_document - a defined record's own docblock becomes a description with no itemdecl") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());

    const ir::SpecItem* descr = class_description(built->document, "tag");
    REQUIRE(descr != nullptr);
    REQUIRE(descr->descr.elements.size() == 1);
    CHECK(descr->descr.elements[0].kind == ir::ElementKind::Remarks);
    CHECK(paragraph_text(descr->descr.elements[0].paragraphs.at(0)) == "A defined record's own description.");
}

TEST_CASE("build_document - a class template's own docblock survives alongside its routed member") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());

    const ir::SpecItem* descr = class_description(built->document, "box");
    REQUIRE(descr != nullptr);
    REQUIRE(descr->descr.elements.size() == 1);
    CHECK(descr->descr.elements[0].kind == ir::ElementKind::Remarks);
}

TEST_CASE("build_document - an authored class \\mandates replaces the derived paragraph and keeps its conjuncts") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());
    const ir::Document& document = built->document;

    const ir::SpecItem* descr = class_description(document, "checked");
    REQUIRE(descr != nullptr);
    const auto mandates = std::ranges::find_if(
        descr->descr.elements, [](const ir::DescriptionElement& e) { return e.kind == ir::ElementKind::Mandates; });
    REQUIRE(mandates != descr->descr.elements.end());
    CHECK_FALSE(mandates->derived);
    CHECK(paragraph_text(mandates->paragraphs.at(0)) == "T is a type this facility accepts.");
    // The suppressed derivation stays as drift evidence, not as a second
    // paragraph saying the same thing.
    REQUIRE(mandates->conjuncts.size() == 1);
    CHECK(paragraph_text(mandates->conjuncts[0]) == "acceptable_v<T> is true");

    for (const ir::Node& node : document.nodes) {
        const auto* free_para = std::get_if<ir::FreeParagraph>(&node);
        if (free_para != nullptr)
            CHECK(paragraph_text(free_para->text).find("checked<T>") == std::string::npos);
    }
}

TEST_CASE("build_document - an unreplaced class derivation keeps its paragraph and the description follows it") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());
    const ir::Document& document = built->document;

    // synopsis(probe), derived paragraph, description-only item, in that order.
    std::optional<std::size_t> synopsis_index;
    for (std::size_t index = 0; index != document.nodes.size(); ++index) {
        const auto* synopsis = std::get_if<ir::Synopsis>(&document.nodes[index]);
        if (synopsis != nullptr && synopsis->name == "probe")
            synopsis_index = index;
    }
    REQUIRE(synopsis_index.has_value());
    REQUIRE(document.nodes.size() > *synopsis_index + 2);

    const auto* general = std::get_if<ir::FreeParagraph>(&document.nodes[*synopsis_index + 1]);
    REQUIRE(general != nullptr);
    CHECK(paragraph_text(general->text).find("probe<T>") != std::string::npos);

    const auto* descr = std::get_if<ir::SpecItem>(&document.nodes[*synopsis_index + 2]);
    REQUIRE(descr != nullptr);
    CHECK(descr->decl.signatures.empty());
    REQUIRE(descr->descr.elements.size() == 1);
    CHECK(descr->descr.elements[0].kind == ir::ElementKind::Remarks);
}
