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
//
// A clause on the template parameter list is the same clause in the other
// equivalent position, and must derive and strip identically (issue #119);
// `put_head`/`put_trailing` in that header write the same two conjuncts each
// way so the two can be compared directly rather than against a transcription.
// `\constraints-in-decl` keeps either one in the itemdecl.

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
const std::string kForeignIncludeHeader    = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_foreign_include.hpp";
const std::string kSharedSpellingHeader    = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_shared_spelling.hpp";

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

// The bare-name complement of the qualifier check (issue #84): `eval` and
// `probe_t` are used unqualified-of-anything-but-their-own-namespace, so no
// written qualifier ever names them foreign, but both resolve to a
// declaration in the included `spec_foreign_detail.hpp` -- and `steppable`,
// declared right beside them, carries `\expos` there and must not join the
// list its uses render under an `\exposid` sentinel instead of its name.
TEST_CASE("build_document - names resolving outside the run, with no qualifier to catch them, are recorded") {
    const auto built = frontend::build_document(kForeignIncludeHeader);
    REQUIRE(built.has_value());
    CHECK(built->diagnostics.empty());

    const auto& found   = built->document.foreign_declarations;
    const auto  names_a = [&](const char* name) {
        return std::ranges::any_of(found, [&](const ir::ForeignDeclaration& d) { return d.name == name; });
    };
    CHECK(names_a("eval"));
    CHECK(names_a("probe_t"));
    CHECK(std::ranges::none_of(found, [](const ir::ForeignDeclaration& d) { return d.name == "steppable"; }));
    CHECK(std::ranges::all_of(found,
                              [](const ir::ForeignDeclaration& d) { return d.header == "spec_foreign_detail.hpp"; }));
}

// The same channel asked about a spelling this run declares (issue #93,
// decision shared-spelling-foreign-name): spec_shared_spelling.hpp documents
// an enumerator `windows_1252` and includes a header declaring a generated
// table of that name, so the resolved reference in the function body is
// foreign and the word is not. Nothing is recorded, and the check the report
// landed on is spared reporting an enumerator as undocumented at the
// enumeration's own declaration.
//
// The namespaces that table sits in *are* still recorded: a written
// `detail::` is wrong in rendered output whatever it qualifies, and it is
// that asymmetry -- the qualifier half untouched, the bare-name half
// filtered -- that makes the empty list a filter on names rather than a
// check switched off.
TEST_CASE("build_document - a foreign name spelled the same as one this run declares is not recorded") {
    const auto built = frontend::build_document(kSharedSpellingHeader);
    REQUIRE(built.has_value());
    CHECK(built->diagnostics.empty());

    CHECK(built->document.foreign_declarations.empty());
    CHECK(std::ranges::any_of(built->document.foreign_namespaces,
                              [](const ir::ForeignNamespace& ns) { return ns.name == "tables"; }));
}

TEST_CASE("build_document - spec_constraints.hpp derives Constraints from a trailing requires-clause") {
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
    REQUIRE(cons->children.size() == 5);

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

TEST_CASE("build_document - a requires-clause derives the same wording from either position") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());

    const auto section = std::ranges::find_if(built->document.nodes, [](const ir::Node& node) {
        const auto* found = std::get_if<ir::Section>(&node);
        return found != nullptr && found->stable_name == "box.cons";
    });
    REQUIRE(section != built->document.nodes.end());
    const ir::Section& cons = std::get<ir::Section>(*section);
    REQUIRE(cons.children.size() == 5);

    // `put_head` writes its clause on the template parameter list, where
    // derive_constraints used to look right past it; `put_trailing` writes the
    // same two conjuncts after the declarator (issue #119).
    const auto* head     = std::get_if<ir::SpecItem>(&cons.children[2]);
    const auto* trailing = std::get_if<ir::SpecItem>(&cons.children[3]);
    REQUIRE(head != nullptr);
    REQUIRE(trailing != nullptr);

    REQUIRE(head->decl.signatures.size() == 1);
    REQUIRE(trailing->decl.signatures.size() == 1);
    CHECK(contains(head->decl.signatures[0].text, "put_head"));
    CHECK(contains(trailing->decl.signatures[0].text, "put_trailing"));
    // Both itemdecls lose the clause; the head form keeps its template head,
    // which the excision has to cut out from under without taking with it.
    CHECK_FALSE(contains(head->decl.signatures[0].text, "requires"));
    CHECK_FALSE(contains(head->decl.signatures[0].text, "is_constructible_v"));
    CHECK(contains(head->decl.signatures[0].text, "template<class U>"));
    CHECK_FALSE(contains(trailing->decl.signatures[0].text, "requires"));

    REQUIRE(head->descr.elements.size() == 2);
    CHECK(head->descr.elements[0].kind == ir::ElementKind::Constraints);
    CHECK(head->descr.elements[0].derived);
    CHECK(head->descr.elements[1].kind == ir::ElementKind::Effects);

    // Two conjuncts is under conjuncts::Options's sentence_threshold, so this
    // pair renders as a joined sentence rather than the itemize above.
    const ir::DescriptionElement& head_constraints     = head->descr.elements[0];
    const ir::DescriptionElement& trailing_constraints = trailing->descr.elements[0];
    CHECK_FALSE(head_constraints.itemize.has_value());
    REQUIRE(head_constraints.paragraphs.size() == 1);
    CHECK(paragraph_text(head_constraints.paragraphs[0]) ==
          "is_constructible_v<T, U> is true and is_same_v<U, int> is false.");

    // The invariant the issue asks for, stated against the other position
    // rather than against a transcription of it.
    REQUIRE(trailing_constraints.paragraphs.size() == 1);
    CHECK(paragraph_text(head_constraints.paragraphs[0]) == paragraph_text(trailing_constraints.paragraphs[0]));
    REQUIRE(head_constraints.conjuncts.size() == trailing_constraints.conjuncts.size());
    CHECK(std::ranges::equal(head_constraints.conjuncts,
                             trailing_constraints.conjuncts,
                             [](const auto& a, const auto& b) { return paragraph_text(a) == paragraph_text(b); }));

    // `\constraints-in-decl` keeps a head clause in the itemdecl and derives
    // nothing from it, the same escape a trailing clause gets.
    const auto* in_decl = std::get_if<ir::SpecItem>(&cons.children[4]);
    REQUIRE(in_decl != nullptr);
    REQUIRE(in_decl->decl.signatures.size() == 1);
    CHECK(contains(in_decl->decl.signatures[0].text, "requires is_convertible_v<U, T>"));
    REQUIRE(in_decl->descr.elements.size() == 1);
    CHECK(in_decl->descr.elements[0].kind == ir::ElementKind::Effects);
}
