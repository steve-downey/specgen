// tests/beman/specgen/frontend/mandates.test.cpp                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// build_document() over the hand-curated corpus header
// (tests/corpus/spec_mandates.hpp) derives a Mandates element from the
// out-of-line definition body's maximal static_assert prefix (design §5.2).
// The `widget.mod` SpecItem's Mandates element must be canonicalized first (it
// sorts ahead of the docblock's own `\effects`), render as a 3-conjunct
// sentence (within conjuncts::Options's sentence_threshold of 3) covering a
// plain trait ("is true"), a top-level `&&` split, and a `!`-negation
// ("is false") in source order. The trailing static_assert past a real
// statement is *not* part of the prefix and must not appear.

#include <beman/specgen/frontend/frontend.hpp>
#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>

namespace frontend = beman::specgen::frontend;
namespace ir       = beman::specgen::ir;

namespace {

const std::string kCorpusHeader = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_mandates.hpp";

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

std::string paragraph_text(const ir::Paragraph& para) {
    std::string out;
    for (const ir::Inline& in : para) {
        if (const auto* text = std::get_if<ir::TextInline>(&in))
            out += text->text;
        else if (const auto* code = std::get_if<ir::CodeInline>(&in))
            out += code->code.text;
        else if (const auto* concept_ref = std::get_if<ir::ConceptRef>(&in))
            out += concept_ref->name;
    }
    return out;
}

} // namespace

TEST_CASE("build_document - class-scope static_asserts become adjacent general wording, not synopsis code") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());
    const ir::Document& document = built->document;

    REQUIRE(document.nodes.size() >= 3);
    const auto* synopsis = std::get_if<ir::Synopsis>(&document.nodes[0]);
    const auto* general  = std::get_if<ir::FreeParagraph>(&document.nodes[1]);
    REQUIRE(synopsis != nullptr);
    REQUIRE(general != nullptr);

    CHECK_FALSE(contains(synopsis->code.text, "static_assert"));
    CHECK_FALSE(contains(synopsis->code.text, "T must be movable"));

    const std::string wording = paragraph_text(general->text);
    CHECK(contains(wording, "A program that instantiates widget<T> is ill-formed unless"));
    CHECK(contains(wording, "is_move_constructible_v<T> is true"));
    CHECK(contains(wording, "is_const_v<T> is false"));
    CHECK(contains(wording, "is_constructible_v<T, T> is true"));
    CHECK_FALSE(contains(wording, "T must be movable"));

    const std::size_t movable      = wording.find("is_move_constructible_v<T>");
    const std::size_t not_constant = wording.find("is_const_v<T>");
    const std::size_t construct    = wording.find("is_constructible_v<T, T>");
    CHECK(movable < not_constant);
    CHECK(not_constant < construct);
}

TEST_CASE("build_document - spec_mandates.hpp derives Mandates from the static_assert prefix") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());
    const ir::Document& document = built->document;

    const ir::Section* mod = nullptr;
    for (const ir::Node& node : document.nodes) {
        if (const auto* section = std::get_if<ir::Section>(&node);
            section != nullptr && section->stable_name == "widget.mod") {
            mod = section;
            break;
        }
    }
    REQUIRE(mod != nullptr);
    REQUIRE(mod->children.size() == 2); // emplace, then shrink (the authored-Mandates case, below)

    const auto* item = std::get_if<ir::SpecItem>(&mod->children[0]);
    REQUIRE(item != nullptr);

    // Descr: Mandates (derived) sorts ahead of Effects (authored), by canonical
    // [structure.specifications] order, regardless of append order.
    REQUIRE(item->descr.elements.size() == 2);
    CHECK(item->descr.elements[0].kind == ir::ElementKind::Mandates);
    CHECK(item->descr.elements[1].kind == ir::ElementKind::Effects);

    // Three conjuncts is within the sentence threshold, so the Mandates renders
    // as one joined sentence (a single paragraph), not an itemize.
    const ir::DescriptionElement& mandates = item->descr.elements[0];
    CHECK_FALSE(mandates.itemize.has_value());
    REQUIRE(mandates.paragraphs.size() == 1);

    const std::string sentence = paragraph_text(mandates.paragraphs[0]);

    // Two static_asserts, the second split at top-level `&&` into a trait and a
    // negation — three conjuncts in source order.
    CHECK(contains(sentence, "is_constructible_v<T, Args...>"));
    CHECK(contains(sentence, "is_move_constructible_v<T>"));
    CHECK(contains(sentence, "is_const_v<T>"));
    CHECK(contains(sentence, "is true"));
    CHECK(contains(sentence, "is false"));
    CHECK_FALSE(contains(sentence, "!is_const_v")); // the `!` is consumed by the phrasing.

    // The maximal prefix stops at the first non-static_assert statement, so the
    // trailing `static_assert(sizeof(T) > 0)` past `value_ = T();` is excluded.
    CHECK_FALSE(contains(sentence, "sizeof"));
}

TEST_CASE("build_document - spec_mandates.hpp authored Mandates replaces derived wording") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());
    const ir::Document& document = built->document;

    const ir::Section* mod = nullptr;
    for (const ir::Node& node : document.nodes) {
        if (const auto* section = std::get_if<ir::Section>(&node);
            section != nullptr && section->stable_name == "widget.mod") {
            mod = section;
            break;
        }
    }
    REQUIRE(mod != nullptr);
    REQUIRE(mod->children.size() == 2);

    const auto* item = std::get_if<ir::SpecItem>(&mod->children[1]);
    REQUIRE(item != nullptr);
    REQUIRE_FALSE(item->decl.signatures.empty());
    REQUIRE(contains(item->decl.signatures[0].text, "shrink"));

    REQUIRE(item->descr.elements.size() == 2);
    CHECK(item->descr.elements[0].kind == ir::ElementKind::Mandates);
    CHECK_FALSE(item->descr.elements[0].derived);
    CHECK(item->descr.elements[1].kind == ir::ElementKind::Effects);

    const ir::DescriptionElement& authored = item->descr.elements[0];
    REQUIRE(authored.paragraphs.size() == 1);
    CHECK(contains(paragraph_text(authored.paragraphs[0]), "No other reference"));
    REQUIRE(authored.conjuncts.size() == 1);
    CHECK(contains(paragraph_text(authored.conjuncts[0]), "is_move_constructible_v<T>"));
}
