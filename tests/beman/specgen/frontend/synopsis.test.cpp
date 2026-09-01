// tests/beman/specgen/frontend/synopsis.test.cpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// build_document() over the hand-curated corpus header
// (tests/corpus/spec_synopsis.hpp) fills the class's ir::Synopsis with
// subtractively extracted code text (design §3.4) — in-class function bodies
// spliced to `;`, `//!`/`/*!` docblocks stripped, everything else
// (`= default`, `= delete`, draft-form `\ref` comments, private data) kept
// verbatim. Assertions are on content, not exact whitespace: the goldens
// pin the clang-format normalization.

#include <beman/specgen/frontend/frontend.hpp>
#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <variant>

namespace frontend = beman::specgen::frontend;
namespace ir       = beman::specgen::ir;

namespace {

const std::string kCorpusHeader           = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_synopsis.hpp";
const std::string kSentinelRecoveryHeader = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_sentinel_recovery.hpp";
const std::string kEmptyRefGroupHeader    = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_empty_ref_group.hpp";

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

} // namespace

TEST_CASE("build_document - empty ref groups are removed without touching live or nested groups") {
    const auto built = frontend::build_document(kEmptyRefGroupHeader);
    REQUIRE(built.has_value());
    REQUIRE_FALSE(built->document.nodes.empty());

    const auto* synopsis = std::get_if<ir::Synopsis>(&built->document.nodes[0]);
    REQUIRE(synopsis != nullptr);
    CHECK_FALSE(contains(synopsis->code.text, "grouping.empty"));
    CHECK_FALSE(contains(synopsis->code.text, "erased_one"));
    CHECK_FALSE(contains(synopsis->code.text, "erased_two"));
    CHECK(contains(synopsis->code.text, "// [grouping.live], visible members"));
    CHECK(contains(synopsis->code.text, "// [grouping.nested], nested members"));

    const auto has_ref = [&](std::string_view target) {
        return std::ranges::any_of(synopsis->code.spans, [&](const ir::Span& span) {
            return span.kind == ir::SpanKind::Ref && span.payload == target;
        });
    };
    CHECK_FALSE(has_ref("grouping.empty"));
    CHECK(has_ref("grouping.live"));
    CHECK(has_ref("grouping.nested"));
}

TEST_CASE("build_document - sentinel recovery handles more than ten spans without overlap") {
    const auto built = frontend::build_document(kSentinelRecoveryHeader);
    REQUIRE(built.has_value());
    REQUIRE_FALSE(built->document.nodes.empty());

    const auto* synopsis = std::get_if<ir::Synopsis>(&built->document.nodes[0]);
    REQUIRE(synopsis != nullptr);
    CHECK(std::ranges::count_if(synopsis->code.spans,
                                [](const ir::Span& span) { return span.kind == ir::SpanKind::ExposId; }) == 11);
    CHECK(std::ranges::count_if(synopsis->code.spans,
                                [](const ir::Span& span) { return span.kind == ir::SpanKind::LibraryIndex; }) == 1);
    CHECK_FALSE(contains(synopsis->code.text, "SPECGEN_"));

    constexpr std::string_view suffix = "terminal_suffix";
    const std::size_t          first  = synopsis->code.text.find(suffix);
    REQUIRE(first != std::string::npos);
    CHECK(synopsis->code.text.find(suffix, first + suffix.size()) == std::string::npos);
}

TEST_CASE("build_document - spec_synopsis.hpp fills the class synopsis with subtractively extracted text") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());
    const ir::Document& document = built->document;
    REQUIRE_FALSE(document.nodes.empty());
    // The class body's own `\ref{number...}` group comments (which become Ref
    // spans in the extracted synopsis, checked below) are real \rSec
    // non-matches, not malformed markers -- they must not produce a
    // diagnostic.
    CHECK(built->diagnostics.empty());

    const auto* synopsis = std::get_if<ir::Synopsis>(&document.nodes[0]);
    REQUIRE(synopsis != nullptr);
    const std::string& text = synopsis->code.text;

    // Kept verbatim: = default, = delete, and the hidden friend's declaration
    // (body spliced, not removed).
    CHECK(contains(text, "= default"));
    CHECK(contains(text, "number(const number&) = default;"));
    CHECK_FALSE(contains(text, "= defaul;"));
    CHECK(contains(text, "= delete"));
    CHECK(contains(text, "friend bool operator=="));

    // The group comment survives, but as a *cross-reference* rather than as
    // the LaTeX macro the header wrote: the text holds what `\ref`
    // renders and a Ref span carries the stable name, so a non-LaTeX backend
    // has something to serialize. Leaving the macro here as raw code
    // text is what design §7 forbids.
    CHECK(contains(text, "// [number.cons], constructors"));
    CHECK_FALSE(contains(text, "\\ref{"));
    const auto ref_span =
        std::ranges::find_if(synopsis->code.spans, [](const ir::Span& s) { return s.kind == ir::SpanKind::Ref; });
    REQUIRE(ref_span != synopsis->code.spans.end());
    CHECK(ref_span->payload == "number.cons");
    CHECK(text.substr(ref_span->begin, ref_span->end - ref_span->begin) == "[number.cons]");

    // Removed: the hidden friend's compound-statement body, and the //!
    // docblock text that documented it.
    CHECK_FALSE(contains(text, "a.value() == b.value()"));
    CHECK_FALSE(contains(text, "\\returns"));

    // `\describe` sends the same ODR-used defaulted constructor through the
    // itemdecl extractor, which must also ignore Clang's synthesized body.
    const auto* cons = std::get_if<ir::Section>(&document.nodes[1]);
    REQUIRE(cons != nullptr);
    REQUIRE(cons->children.size() == 2);
    const auto* copy = std::get_if<ir::SpecItem>(&cons->children[1]);
    REQUIRE(copy != nullptr);
    REQUIRE(copy->decl.signatures.size() == 1);
    CHECK(copy->decl.signatures[0].text == "number(const number&) = default;");
}
