// tests/beman/specgen/frontend/macro_spellings.test.cpp          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/macro_spellings.hpp>
#include <beman/specgen/frontend/macro_spellings.hpp> // Re-inclusion verification

#include <beman/specgen/frontend/build_document.hpp>
#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace frontend = beman::specgen::frontend;
namespace ir       = beman::specgen::ir;

namespace {

const std::map<std::string, std::string> kMacros{{"DEMO_CONSTEXPR", "constexpr"}, {"DEMO_NOTHING", ""}};

} // namespace

TEST_CASE("macro_spellings - a specifier macro is written as it expands") {
    const auto expanded = [](std::string text) {
        return frontend::expand_specifier_macros(ir::CodeText{std::move(text), {}}, kMacros).text;
    };
    CHECK(expanded("inline DEMO_CONSTEXPR int f() const;") == "inline constexpr int f() const;");
    CHECK(expanded("inline DEMO_NOTHING int g() const;") == "inline int g() const;");
    CHECK(expanded("  DEMO_NOTHING int k() const;") == "  int k() const;");
    CHECK(expanded("int k() const DEMO_NOTHING;") == "int k() const;");
    CHECK(expanded("int MY_DEMO_CONSTEXPR(); int DEMO_CONSTEXPRx;") ==
          "int MY_DEMO_CONSTEXPR(); int DEMO_CONSTEXPRx;");
}

TEST_CASE("macro_spellings - a comment, a literal and a span are not code to expand") {
    ir::CodeText code{
        "DEMO_CONSTEXPR int @x@; // DEMO_NOTHING\nconst char* s = \"DEMO_NOTHING\"; /* DEMO_CONSTEXPR */", {}};
    const std::size_t at   = code.text.find("@x@");
    code.spans             = {{at, at + 3, ir::SpanKind::ExposId, "x"}};
    const ir::CodeText out = frontend::expand_specifier_macros(code, kMacros);
    CHECK(out.text == "constexpr int @x@; // DEMO_NOTHING\nconst char* s = \"DEMO_NOTHING\"; /* DEMO_CONSTEXPR */");
    REQUIRE(out.spans.size() == 1);
    CHECK(out.text.substr(out.spans[0].begin, out.spans[0].end - out.spans[0].begin) == "@x@");
}

TEST_CASE("macro_spellings - the synopsis and the item declarations of a header read as they expand") {
    const auto built = frontend::build_document(std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_macro_specifiers.hpp");
    REQUIRE(built.has_value());
    const auto& nodes = built->document.nodes;
    const auto  found = std::ranges::find_if(nodes, [](const ir::Node& node) {
        const auto* section = std::get_if<ir::Section>(&node);
        return section != nullptr && section->stable_name == "w.thing";
    });
    REQUIRE(found != nodes.end());
    const auto&              section = std::get<ir::Section>(*found);
    std::vector<std::string> codes;
    std::ranges::for_each(section.children, [&](const ir::Node& node) {
        if (const auto* synopsis = std::get_if<ir::Synopsis>(&node))
            codes.push_back(synopsis->code.text);
        if (const auto* item = std::get_if<ir::SpecItem>(&node))
            std::ranges::for_each(item->decl.signatures, [&](const ir::CodeText& c) { codes.push_back(c.text); });
    });
    const auto has = [&](std::string_view needle) {
        return std::ranges::any_of(codes, [&](const std::string& c) { return c.contains(needle); });
    };
    CHECK(has("inline constexpr int f() const;"));
    CHECK(has("inline int g() const;"));
    CHECK(has("constexpr int h() const;"));
    CHECK(has("int m() const noexcept;"));
    CHECK(has("int k() const;"));
    CHECK(has("DEMO_VALUE"));
    CHECK_FALSE(has("DEMO_CONSTEXPR"));
    CHECK_FALSE(has("DEMO_INDIRECT"));
    CHECK_FALSE(has("DEMO_NOEXCEPT"));
    CHECK_FALSE(has("DEMO_NOTHING int"));
}
