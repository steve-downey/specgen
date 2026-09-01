// tests/beman/specgen/frontend/doxygen.test.cpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// `///` and `/** */` are Doxygen's, and specgen reads neither.
// build_document() over tests/corpus/spec_doxygen.hpp, which is
// spec_widget.hpp with a Doxygen block added at each position a real header
// puts one (its own top comment lists them).
//
// The classification guards against two defects, and they pull in
// opposite directions, so both halves are asserted here:
//
//   - `///` is not *markup*. Pointing the docblock grammar at API
//     documentation would report `prose before first element tag` and
//     build an itemdescr out of prose the draft would never print. So: the
//     document carries no diagnostic at all, even though the fixture's `///`
//     block names `\effect`, which is reported as an unknown-tag Error the
//     moment anything parses it.
//   - `/** */` is not *draft-form*, and draft-form means kept — and kept
//     means printed into the synopsis. So: no Doxygen decoration and no
//     Doxygen prose survives in `synopsis.code.text`.
//
// A test that only asserted the first would pass just as well for a front
// end that kept `///` in the synopsis, which is the worse of the two bugs.
// `golden.doxygen` watches the pair from the other end, and more strictly
// than anything here can: its `expected.json` is `widget_skeleton`'s modulo
// the rename.

#include <beman/specgen/frontend/frontend.hpp>
#include <beman/specgen/ir.hpp>

#include <string>
#include <string_view>
#include <variant>

#include <catch2/catch_test_macros.hpp>

namespace frontend = beman::specgen::frontend;
namespace ir       = beman::specgen::ir;

namespace {

const std::string kCorpusHeader = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_doxygen.hpp";

bool contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

// The first SpecItem in any section whose primary signature mentions
// `signature`, or nullptr.
const ir::SpecItem* item_for(const ir::Document& document, std::string_view signature) {
    for (const ir::Node& node : document.nodes) {
        const auto* section = std::get_if<ir::Section>(&node);
        if (section == nullptr)
            continue;
        for (const ir::Node& child : section->children) {
            const auto* item = std::get_if<ir::SpecItem>(&child);
            if (item == nullptr || item->decl.signatures.empty())
                continue;
            if (contains(item->decl.signatures.front().text, signature))
                return item;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("build_document - a Doxygen comment is not read as specgen markup") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());

    // Reading the fixture's `///` blocks as markup costs an unknown-tag Error
    // for `\effect` plus a `prose before first element tag` finding
    // for every Doxygen sentence ahead of it.
    CHECK(built->diagnostics.empty());
}

TEST_CASE("build_document - no Doxygen comment reaches the synopsis") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());
    const ir::Document& document = built->document;
    REQUIRE_FALSE(document.nodes.empty());

    const auto* synopsis = std::get_if<ir::Synopsis>(&document.nodes[0]);
    REQUIRE(synopsis != nullptr);
    const std::string& text = synopsis->code.text;

    // Neither spelling's decoration, and neither one's prose.
    CHECK_FALSE(contains(text, "///"));
    CHECK_FALSE(contains(text, "/**"));
    CHECK_FALSE(contains(text, "Constructs an empty gadget"));
    CHECK_FALSE(contains(text, "Whether the gadget holds no value"));
    CHECK_FALSE(contains(text, "Storage for the held value"));

    // Draft-form comments still survive verbatim, including the
    // `\ref{gadget.cons}` header that Clang merged into the *same*
    // RawComment as the two `///` lines under it — as the Ref spans a
    // group header turns into.
    CHECK(contains(text, "// [gadget.cons], constructors"));
    CHECK(contains(text, "// [gadget.observers], observers"));

    // And the declarations themselves are untouched, with no gap left where
    // a stripped block sat: this is `spec_widget.hpp`'s synopsis, renamed.
    CHECK(contains(text, "// [gadget.cons], constructors\n  gadget();\n  explicit gadget(int value);"));
    CHECK(contains(text, "// [gadget.observers], observers\n  bool empty() const;"));
}

TEST_CASE("build_document - specgen markup below a Doxygen comment is still read") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());

    // All three out-of-line definitions carry a Doxygen block directly above
    // their `//!` docblock. The line form is the sharper of the two: Clang
    // merges `///` with the `//!` under it into one RawComment, so a front
    // end that keyed on the comment's *first* line would find Doxygen and
    // read no markup at all.
    const ir::SpecItem* ctor = item_for(built->document, "gadget();");
    REQUIRE(ctor != nullptr);
    REQUIRE(ctor->descr.elements.size() == 1);
    CHECK(ctor->descr.elements.front().kind == ir::ElementKind::Effects);

    // Two elements here, and the count is the assertion: a Doxygen block read
    // as markup would contribute a third made of prose.
    const ir::SpecItem* observer = item_for(built->document, "bool empty() const;");
    REQUIRE(observer != nullptr);
    REQUIRE(observer->descr.elements.size() == 2);
    CHECK(observer->descr.elements[0].kind == ir::ElementKind::Effects);
    CHECK(observer->descr.elements[1].kind == ir::ElementKind::Returns);
}
