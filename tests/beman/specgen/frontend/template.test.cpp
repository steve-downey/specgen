// tests/beman/specgen/frontend/template.test.cpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// build_document() over the hand-curated corpus header
// (tests/corpus/spec_template.hpp) recognizes a ClassTemplateDecl the same
// way it recognizes a plain CXXRecordDecl (design §3.6 checkpoint's "latent
// gap" fix) — `box` gets a populated ir::Synopsis, not an empty ir::SpecItem
// — and the extracted, clang-formatted (design §3.6 step 2) text starts
// at the `template` keyword and keeps the requires-clause, but not the
// out-of-line member's body.
//
// The corpus header's first top-level decl is the namespace-scope `regular`
// concept, which yields an
// empty ir::SpecItem ahead of `box`'s ir::Synopsis — so this looks for the
// first Synopsis in document order rather than assuming index 0.

#include <beman/specgen/frontend/frontend.hpp>
#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>

namespace frontend = beman::specgen::frontend;
namespace ir       = beman::specgen::ir;

namespace {

const std::string kCorpusHeader = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_template.hpp";

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

} // namespace

TEST_CASE("build_document - spec_template.hpp recognizes the class template as a Synopsis") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());
    const ir::Document& document = built->document;

    const ir::Synopsis* synopsis = nullptr;
    for (const ir::Node& node : document.nodes) {
        if (const auto* found = std::get_if<ir::Synopsis>(&node)) {
            synopsis = found;
            break;
        }
    }
    REQUIRE(synopsis != nullptr);
    const std::string& text = synopsis->code.text;

    // Extraction range starts at `template`, not `class`, and keeps the
    // requires-clause; the draft FormatStyle (design §3.6 step 2) removes the
    // post-template space while keeping a record's template head on its own
    // line. Short function-template declarations may share one line.
    CHECK(text.starts_with("template<class T>\n"));
    CHECK_FALSE(text.starts_with("template<class T> class box"));
    CHECK_FALSE(contains(text, "template <"));
    CHECK(contains(text, "requires regular<T>"));
    CHECK(contains(text, "T get() const;"));

    // The out-of-line member's body is not part of the class's own text.
    CHECK_FALSE(contains(text, "return value_"));
}
