// tests/beman/specgen/backend/render_fold.test.cpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/backend/render_fold.hpp>
#include <beman/specgen/backend/render_fold.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>
#include <vector>

using namespace beman::specgen::ir;
namespace backend = beman::specgen::backend;

// render_fmap maps a layer's one recursive position and leaves the rest alone.

TEST_CASE("render_fold - render_fmap maps a section's children and keeps its header") {
    const backend::RenderF<int> layer = backend::RenderedSectionF<int>{"header", {1, 2, 3}};
    const auto  out     = backend::render_fmap([](int child) { return std::to_string(child * 10); }, layer);
    const auto* section = std::get_if<backend::RenderedSectionF<std::string>>(&out);
    REQUIRE(section != nullptr);
    CHECK(section->header == "header");
    CHECK(section->children == std::vector<std::string>{"10", "20", "30"});
}

TEST_CASE("render_fold - render_fmap passes a leaf through unchanged") {
    FreeParagraph paragraph;
    paragraph.text.push_back(TextInline{"prose"});
    const backend::RenderF<int> layer = paragraph;
    const auto                  out   = backend::render_fmap([](int child) { return std::to_string(child); }, layer);
    const auto*                 leaf  = std::get_if<FreeParagraph>(&out);
    REQUIRE(leaf != nullptr);
    REQUIRE(leaf->text.size() == 1);
    CHECK(std::get<TextInline>(leaf->text.front()).text == "prose");
}
