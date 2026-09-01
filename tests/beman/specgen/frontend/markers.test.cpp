// tests/beman/specgen/frontend/markers.test.cpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// build_document() over the hand-curated corpus header
// (tests/corpus/spec_markers.hpp) acts on bare and named `\also`, `\group`,
// and `\omit` (design §4.3).
// `front()` is described and starts a group; `front() const` carries `\also`
// and joins its signature onto the group's primary instead of starting a new
// SpecItem. Interleaved ref-qualified `read()` overloads form two named
// groups; `scratch()` carries `\omit` and produces no SpecItem at all; `size()`
// is a plain, undisturbed described member.

#include <beman/specgen/frontend/frontend.hpp>
#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>

namespace frontend = beman::specgen::frontend;
namespace ir       = beman::specgen::ir;

namespace {

const std::string kCorpusHeader = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_markers.hpp";

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

} // namespace

TEST_CASE("build_document - spec_markers.hpp groups \\also overloads and drops \\omit") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());
    CHECK(built->diagnostics.empty());
    const ir::Document& document = built->document;

    const auto* synopsis = std::get_if<ir::Synopsis>(&document.nodes[0]);
    REQUIRE(synopsis != nullptr);
    CHECK(contains(synopsis->code.text, "int read() &;  // freestanding"));
    CHECK(contains(synopsis->code.text, "int read() &&; // freestanding-deleted"));

    const ir::Section* access = nullptr;
    for (const ir::Node& node : document.nodes) {
        if (const auto* section = std::get_if<ir::Section>(&node);
            section != nullptr && section->stable_name == "span.access") {
            access = section;
            break;
        }
    }
    REQUIRE(access != nullptr);

    // Four SpecItems: the adjacent `front` pair, two named `read` groups, and
    // `size`. `scratch` is omitted entirely.
    REQUIRE(access->children.size() == 4);

    const auto* front  = std::get_if<ir::SpecItem>(&access->children[0]);
    const auto* lvalue = std::get_if<ir::SpecItem>(&access->children[1]);
    const auto* rvalue = std::get_if<ir::SpecItem>(&access->children[2]);
    const auto* size   = std::get_if<ir::SpecItem>(&access->children[3]);
    REQUIRE(front != nullptr);
    REQUIRE(lvalue != nullptr);
    REQUIRE(rvalue != nullptr);
    REQUIRE(size != nullptr);

    // `front`: both signatures grouped onto one itemdecl block.
    REQUIRE(front->decl.signatures.size() == 2);
    CHECK(contains(front->decl.signatures[0].text, "int& front()"));
    CHECK_FALSE(contains(front->decl.signatures[0].text, "const"));
    CHECK(contains(front->decl.signatures[1].text, "const int& front() const"));

    REQUIRE(front->descr.elements.size() == 1);
    CHECK(front->descr.elements[0].kind == ir::ElementKind::Returns);

    // The declaration/definition order is &, &&, const&, const&&. Named
    // grouping reaches across the intervening primary without changing the
    // signature order within either resulting item.
    REQUIRE(lvalue->decl.signatures.size() == 2);
    CHECK(contains(lvalue->decl.signatures[0].text, "int read() &"));
    CHECK(contains(lvalue->decl.signatures[1].text, "int read() const&"));
    REQUIRE(rvalue->decl.signatures.size() == 2);
    CHECK(contains(rvalue->decl.signatures[0].text, "int read() &&"));
    CHECK(contains(rvalue->decl.signatures[1].text, "int read() const&&"));

    // `size`: untouched, single signature, single \effects element.
    REQUIRE(size->decl.signatures.size() == 1);
    CHECK(contains(size->decl.signatures[0].text, "int size() const"));
    REQUIRE(size->descr.elements.size() == 1);
    CHECK(size->descr.elements[0].kind == ir::ElementKind::Effects);

    // `scratch` is omitted: no SpecItem's signature mentions it.
    for (const ir::Node& child : access->children) {
        const auto* item = std::get_if<ir::SpecItem>(&child);
        REQUIRE(item != nullptr);
        for (const ir::CodeText& sig : item->decl.signatures)
            CHECK_FALSE(contains(sig.text, "scratch"));
    }
}
