// tests/beman/specgen/frontend/expos_uses.test.cpp               -*-C++-*-
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
const std::string kCorpusHeader       = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_expos_uses.hpp";
const std::string kMemberCorpusHeader = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_expos.hpp";
} // namespace

TEST_CASE("build_document - anonymous-union exposition members remain in the synopsis and roster") {
    const auto built = frontend::build_document(kMemberCorpusHeader);
    REQUIRE(built.has_value());

    const auto* synopsis = std::get_if<ir::Synopsis>(&built->document.nodes.front());
    REQUIRE(synopsis != nullptr);
    CHECK(synopsis->code.text.contains("union {"));
    CHECK(synopsis->code.text.contains("T value; // exposition only"));
    CHECK_FALSE(synopsis->code.text.contains("spare_"));
    CHECK(std::ranges::count(synopsis->code.spans, std::string("value"), &ir::Span::payload) == 1);
    CHECK(std::ranges::count(synopsis->code.spans, std::string("size"), &ir::Span::payload) == 1);
    CHECK(synopsis->code.text.contains("void convert-ref-init-val(U&& value); // exposition only"));
    CHECK(std::ranges::count(synopsis->code.spans, std::string("convert-ref-init-val"), &ir::Span::payload) == 1);

    CHECK(std::ranges::count(synopsis->roster, std::string("value_"), &ir::SynopsisEntry::name) == 1);
    const auto value = std::ranges::find(synopsis->roster, std::string("value_"), &ir::SynopsisEntry::name);
    REQUIRE(value != synopsis->roster.end());
    CHECK(value->kind == ir::MemberKind::Data);
    CHECK(value->disposition == ir::Disposition::Expos);

    CHECK(std::ranges::find(synopsis->roster, std::string("spare_"), &ir::SynopsisEntry::name) ==
          synopsis->roster.end());

    const auto helper =
        std::ranges::find(synopsis->roster, std::string("convert_ref_init_val"), &ir::SynopsisEntry::name);
    REQUIRE(helper != synopsis->roster.end());
    CHECK(helper->kind == ir::MemberKind::Function);
    CHECK(helper->disposition == ir::Disposition::Routed);
    CHECK(helper->section == "holder.expos");

    const auto observer_node = std::ranges::find_if(built->document.nodes, [](const ir::Node& node) {
        const auto* section = std::get_if<ir::Section>(&node);
        return section != nullptr && section->stable_name == "holder.obs";
    });
    REQUIRE(observer_node != built->document.nodes.end());
    const auto* observers = std::get_if<ir::Section>(&*observer_node);
    REQUIRE(observers != nullptr);
    REQUIRE(observers->children.size() == 2);
    const auto& get = std::get<ir::SpecItem>(observers->children.front());
    REQUIRE(get.descr.elements.size() == 1);
    const ir::Paragraph& effects = get.descr.elements.front().paragraphs.front();
    const auto           code_inline =
        std::ranges::find_if(effects, [](const ir::Inline& in) { return std::holds_alternative<ir::CodeInline>(in); });
    REQUIRE(code_inline != effects.end());
    const auto* code = std::get_if<ir::CodeInline>(&*code_inline);
    REQUIRE(code != nullptr);
    REQUIRE(code->code.spans.size() == 1);
    CHECK(code->code.spans.front().kind == ir::SpanKind::ExposId);
    CHECK(code->code.spans.front().payload == "value");

    const auto& set = std::get<ir::SpecItem>(observers->children.back());
    REQUIRE(set.descr.elements.front().equivalent.has_value());
    const ir::CodeText& set_body = set.descr.elements.front().equivalent->code;
    CHECK(set_body.text.contains("convert-ref-init-val(value);"));
    CHECK(std::ranges::count(set_body.spans, std::string("convert-ref-init-val"), &ir::Span::payload) == 1);

    const auto expos_node = std::ranges::find_if(built->document.nodes, [](const ir::Node& node) {
        const auto* section = std::get_if<ir::Section>(&node);
        return section != nullptr && section->stable_name == "holder.expos";
    });
    REQUIRE(expos_node != built->document.nodes.end());
    const ir::Section& expos_section = std::get<ir::Section>(*expos_node);
    REQUIRE(expos_section.children.size() == 1);
    const ir::SpecItem& expos_item = std::get<ir::SpecItem>(expos_section.children.front());
    REQUIRE(expos_item.decl.signatures.size() == 1);
    const ir::CodeText& expos_decl = expos_item.decl.signatures.front();
    CHECK(expos_decl.text.contains("void convert-ref-init-val(U&& value);"));
    CHECK(std::ranges::count(expos_decl.spans, std::string("convert-ref-init-val"), &ir::Span::payload) == 1);
}

TEST_CASE("build_document - Equivalent-to bodies resolve namespace exposition uses") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());

    const ir::Synopsis* variable  = nullptr;
    const ir::Section*  observers = nullptr;
    for (const ir::Node& node : built->document.nodes) {
        if (const auto* synopsis = std::get_if<ir::Synopsis>(&node);
            synopsis != nullptr && synopsis->name == "exposed_limit")
            variable = synopsis;
        if (const auto* section = std::get_if<ir::Section>(&node);
            section != nullptr && section->stable_name == "counter.obs")
            observers = section;
    }

    REQUIRE(variable != nullptr);
    CHECK(variable->code.text.contains("inline constexpr int limit = 1;"));
    REQUIRE(variable->code.spans.size() == 1);
    CHECK(variable->code.spans.front().payload == "limit");

    REQUIRE(observers != nullptr);
    REQUIRE(observers->children.size() == 3);
    // The third child is apply_in_context: its parameter is written
    // `detail::traverse_context_t<int>` and must render as the exposid
    // spelling with the qualifier dropped — the type-use half of §3.5.
    const auto&         apply      = std::get<ir::SpecItem>(observers->children[2]);
    const ir::CodeText& apply_decl = apply.decl.signatures.front();
    CHECK(apply_decl.text.contains("traverse-context-t<int> context"));
    CHECK_FALSE(apply_decl.text.contains("detail::"));
    CHECK(std::ranges::count(apply_decl.spans, std::string("traverse-context-t"), &ir::Span::payload) == 1);
    const auto& bump = std::get<ir::SpecItem>(observers->children[1]);
    const auto  effects =
        std::ranges::find(bump.descr.elements, ir::ElementKind::Effects, &ir::DescriptionElement::kind);
    REQUIRE(effects != bump.descr.elements.end());
    REQUIRE(effects->equivalent.has_value());
    const ir::CodeText& code = effects->equivalent->code;

    CHECK_FALSE(code.text.contains("detail::"));
    CHECK_FALSE(code.text.contains("static_assert"));
    CHECK(code.text.contains("int exposed_limit = 2;"));
    CHECK(code.text.contains("if constexpr (enabled<int>)"));
    CHECK(code.text.contains("value = value + limit + exposed_limit;"));
    CHECK(std::ranges::count(code.spans, std::string("enabled"), &ir::Span::payload) == 1);
    CHECK(std::ranges::count(code.spans, std::string("limit"), &ir::Span::payload) == 1);
    CHECK(std::ranges::count(code.spans, std::string("value"), &ir::Span::payload) == 2);
}
