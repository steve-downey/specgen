// tests/beman/specgen/frontend/constraints.test.cpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// build_document() over the hand-curated corpus header
// (tests/corpus/spec_constraints.hpp) derives a Constraints element from the
// out-of-line definition's trailing requires-clause (design §5.1). The
// `box.cons` SpecItem's Constraints element must be canonicalized first (it
// sorts ahead of the docblock's own `\effects`), render as a 4-item itemize
// (past conjuncts::Options's sentence_threshold of 3) phrasing every case —
// concept-id ("`X` models C"), plain trait ("is true"), parenthesized
// negation ("is false"), plain trait again — in source order, and the
// itemdecl must have the requires-clause stripped (design §5.1: "requires-
// clause is removed from the itemdecl").

#include <beman/specgen/frontend/frontend.hpp>
#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <variant>

namespace frontend = beman::specgen::frontend;
namespace ir       = beman::specgen::ir;

namespace {

const std::string kCorpusHeader            = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_constraints.hpp";
const std::string kImportedQualifierHeader = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_imported_qualifier.hpp";

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

TEST_CASE("build_document - a qualifier naming an imported standard declaration is removed per use") {
    const auto built = frontend::build_document(kImportedQualifierHeader);
    REQUIRE(built.has_value());
    CHECK(built->diagnostics.empty());

    const auto section = std::ranges::find_if(built->document.nodes, [](const ir::Node& node) {
        const auto* found = std::get_if<ir::Section>(&node);
        return found != nullptr && found->stable_name == "probe.obs";
    });
    REQUIRE(section != built->document.nodes.end());
    const ir::Section& observers = std::get<ir::Section>(*section);
    REQUIRE(observers.children.size() == 1);
    const auto* item = std::get_if<ir::SpecItem>(&observers.children.front());
    REQUIRE(item != nullptr);
    REQUIRE(item->descr.elements.size() == 1);
    REQUIRE(item->descr.elements.front().equivalent.has_value());

    const std::string& code = item->descr.elements.front().equivalent->code.text;
    CHECK(contains(code, "imported_trait_v<T>"));
    CHECK_FALSE(contains(code, "detail::imported_trait_v"));
    CHECK_FALSE(contains(code, "imported_detail::"));
    CHECK(contains(code, "detail::local_trait_v<T>"));
    CHECK(std::ranges::none_of(built->document.foreign_namespaces,
                               [](const ir::ForeignNamespace& ns) { return ns.name == "imported_detail"; }));
    CHECK(std::ranges::any_of(built->document.foreign_namespaces,
                              [](const ir::ForeignNamespace& ns) { return ns.name == "detail"; }));
}

TEST_CASE("build_document - spec_constraints.hpp derives Constraints from the trailing requires-clause") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());
    const ir::Document& document = built->document;

    const ir::Section* cons = nullptr;
    for (const ir::Node& node : document.nodes) {
        if (const auto* section = std::get_if<ir::Section>(&node);
            section != nullptr && section->stable_name == "box.cons") {
            cons = section;
            break;
        }
    }
    REQUIRE(cons != nullptr);
    REQUIRE(cons->children.size() == 2);

    const auto* item = std::get_if<ir::SpecItem>(&cons->children[0]);
    REQUIRE(item != nullptr);

    // Itemdecl: the requires-clause is gone, and with it every trait/concept
    // name it named.
    REQUIRE(item->decl.signatures.size() == 1);
    const std::string& itemdecl_text = item->decl.signatures[0].text;
    CHECK_FALSE(contains(itemdecl_text, "requires"));
    CHECK_FALSE(contains(itemdecl_text, "copyable"));
    CHECK(contains(itemdecl_text, "box(const box<U>& other)"));

    // Descr: Constraints (derived) sorts ahead of Effects (authored), by
    // canonical [structure.specifications] order, regardless of the order
    // build_spec_item appended them in.
    REQUIRE(item->descr.elements.size() == 2);
    CHECK(item->descr.elements[0].kind == ir::ElementKind::Constraints);
    CHECK(item->descr.elements[1].kind == ir::ElementKind::Effects);

    const ir::DescriptionElement& constraints = item->descr.elements[0];
    CHECK(constraints.paragraphs.empty());
    REQUIRE(constraints.itemize.has_value());
    REQUIRE(constraints.itemize->items.size() == 4);

    const std::string concept_item  = paragraph_text(constraints.itemize->items[0]);
    const std::string trait_item    = paragraph_text(constraints.itemize->items[1]);
    const std::string negation_item = paragraph_text(constraints.itemize->items[2]);
    const std::string trait2_item   = paragraph_text(constraints.itemize->items[3]);

    // Concept-id uses the WG21 "`X` models C" form (the concept name renders
    // as a ConceptRef → \libconcept{}), not "`C<X>` is satisfied".
    CHECK(contains(concept_item, "U models copyable"));
    CHECK_FALSE(contains(concept_item, "satisfied"));

    CHECK(contains(trait_item, "is_constructible_v<T, const U&>"));
    CHECK(contains(trait_item, "is true"));

    CHECK(contains(negation_item, "is_same_v<T, U>"));
    CHECK(contains(negation_item, "is false"));
    CHECK_FALSE(contains(negation_item, "!is_same_v"));

    CHECK(contains(trait2_item, "is_convertible_v<U, T>"));
    CHECK(contains(trait2_item, "is true"));

    const auto* authored_item = std::get_if<ir::SpecItem>(&cons->children[1]);
    REQUIRE(authored_item != nullptr);
    REQUIRE(authored_item->descr.elements.size() == 2);
    const ir::DescriptionElement& authored_constraints = authored_item->descr.elements[0];
    CHECK(authored_constraints.kind == ir::ElementKind::Constraints);
    CHECK_FALSE(authored_constraints.derived);
    REQUIRE(authored_constraints.paragraphs.size() == 1);
    CHECK(contains(paragraph_text(authored_constraints.paragraphs[0]), "U is not void"));
    REQUIRE(authored_constraints.conjuncts.size() == 2);
    CHECK(contains(paragraph_text(authored_constraints.conjuncts[0]), "is-compatible<T, U> is satisfied"));
    CHECK(contains(paragraph_text(authored_constraints.conjuncts[1]), "is-allowed<U> is true"));

    const auto& derived_code = std::get<ir::CodeInline>(authored_constraints.conjuncts[0][0]).code;
    CHECK_FALSE(contains(derived_code.text, "detail::"));
    REQUIRE(derived_code.spans.size() == 1);
    CHECK(derived_code.spans[0].kind == ir::SpanKind::ExposId);
    CHECK(derived_code.spans[0].payload == "is-compatible");

    const auto& variable_code = std::get<ir::CodeInline>(authored_constraints.conjuncts[1][0]).code;
    REQUIRE(variable_code.spans.size() == 1);
    CHECK(variable_code.spans[0].kind == ir::SpanKind::ExposId);
    CHECK(variable_code.spans[0].payload == "is-allowed");

    const ir::Synopsis* synopsis = std::get_if<ir::Synopsis>(&built->document.nodes[0]);
    REQUIRE(synopsis != nullptr);
    CHECK_FALSE(contains(synopsis->code.text, "detail::is_compatible"));
    CHECK(std::ranges::any_of(synopsis->code.spans,
                              [](const ir::Span& span) { return span.kind == ir::SpanKind::ExposId; }));
}
