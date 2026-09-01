// tests/beman/specgen/frontend/attach.test.cpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// build_document() over the hand-curated corpus header
// (tests/corpus/spec_attach.hpp) fills a SpecItem's ItemDecl and ItemDescr
// for out-of-line function definitions (design §3.3: redeclaration-chain
// attachment). The key assertion is overload disambiguation: `top()` and
// `top() const` are two separate out-of-line definitions sharing one
// in-class `\ref` group, and each SpecItem's itemdecl must come from its own
// in-class declaration — unqualified (no `stack::`) — not its sibling's.

#include <beman/specgen/frontend/frontend.hpp>
#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <variant>

namespace frontend = beman::specgen::frontend;
namespace ir       = beman::specgen::ir;

namespace {

const std::string kCorpusHeader          = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_attach.hpp";
const std::string kInclassTemplateHeader = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_inclass_template.hpp";
const std::string kFreeFunctionsHeader   = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_free_functions.hpp";

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

} // namespace

TEST_CASE("build_document - documented namespace free-function definitions become ordered items") {
    const auto built = frontend::build_document(kFreeFunctionsHeader);
    REQUIRE(built.has_value());
    CHECK(built->diagnostics.empty());

    const auto section = std::ranges::find_if(built->document.nodes, [](const ir::Node& node) {
        const auto* found = std::get_if<ir::Section>(&node);
        return found != nullptr && found->stable_name == "free.functions";
    });
    REQUIRE(section != built->document.nodes.end());
    const ir::Section& functions = std::get<ir::Section>(*section);
    REQUIRE(functions.children.size() == 2);

    const auto* identity = std::get_if<ir::SpecItem>(&functions.children[0]);
    const auto* inspect  = std::get_if<ir::SpecItem>(&functions.children[1]);
    REQUIRE(identity != nullptr);
    REQUIRE(inspect != nullptr);

    REQUIRE(identity->decl.signatures.size() == 1);
    CHECK(contains(identity->decl.signatures.front().text, "template<class T> constexpr T identity(T value);"));
    CHECK_FALSE(contains(identity->decl.signatures.front().text, "template <"));
    CHECK_FALSE(contains(identity->decl.signatures.front().text, "return value"));
    REQUIRE(identity->descr.elements.size() == 1);
    CHECK(identity->descr.elements.front().kind == ir::ElementKind::Effects);
    REQUIRE(identity->decl.index.size() == 1);
    CHECK(identity->decl.index.front().kind == ir::IndexKind::Global);
    CHECK(identity->decl.index.front().name == "identity");
    CHECK(identity->decl.index.front().parent.empty());

    REQUIRE(inspect->decl.signatures.size() == 1);
    CHECK(contains(inspect->decl.signatures.front().text, "int inspect(int declared_value);"));
    CHECK_FALSE(contains(inspect->decl.signatures.front().text, "definition_value"));
    REQUIRE(inspect->descr.elements.size() == 1);
    CHECK(inspect->descr.elements.front().kind == ir::ElementKind::Returns);
    REQUIRE(inspect->descr.elements.front().equivalent.has_value());
    CHECK(contains(inspect->descr.elements.front().equivalent->code.text, "return definition_value;"));
    REQUIRE(inspect->decl.index.size() == 1);
    CHECK(inspect->decl.index.front().kind == ir::IndexKind::Global);
    CHECK(inspect->decl.index.front().name == "inspect");
    CHECK(inspect->decl.index.front().parent.empty());

    const auto contains_helper = [](const ir::Node& node) {
        const auto* item = std::get_if<ir::SpecItem>(&node);
        return item != nullptr && std::ranges::any_of(item->decl.signatures, [](const ir::CodeText& signature) {
                   return signature.text.contains("implementation_helper");
               });
    };
    CHECK_FALSE(std::ranges::any_of(functions.children, contains_helper));
}

TEST_CASE("build_document - marked in-class function templates attach and mixed-comment merges are observed") {
    const auto built = frontend::build_document(kInclassTemplateHeader);
    REQUIRE(built.has_value());
    CHECK(built->diagnostics.empty());

    const auto synopsis = std::ranges::find_if(
        built->document.nodes, [](const ir::Node& node) { return std::holds_alternative<ir::Synopsis>(node); });
    REQUIRE(synopsis != built->document.nodes.end());
    const ir::Synopsis& syn = std::get<ir::Synopsis>(*synopsis);
    CHECK_FALSE(contains(syn.code.text, "converter(int)"));
    CHECK_FALSE(contains(syn.code.text, "merged(T)"));

    const auto convert_entry = std::ranges::find(syn.roster, std::string("convert"), &ir::SynopsisEntry::name);
    REQUIRE(convert_entry != syn.roster.end());
    CHECK(convert_entry->disposition == ir::Disposition::Routed);
    CHECK(convert_entry->section == "converter.ops");

    CHECK(std::ranges::count(syn.roster, ir::Disposition::Merged, &ir::SynopsisEntry::disposition) == 2);

    const auto section = std::ranges::find_if(built->document.nodes, [](const ir::Node& node) {
        const auto* found = std::get_if<ir::Section>(&node);
        return found != nullptr && found->stable_name == "converter.ops";
    });
    REQUIRE(section != built->document.nodes.end());
    const ir::Section& operations = std::get<ir::Section>(*section);
    REQUIRE(operations.children.size() == 1);
    const auto* item = std::get_if<ir::SpecItem>(&operations.children.front());
    REQUIRE(item != nullptr);
    REQUIRE(item->decl.signatures.size() == 1);
    CHECK(contains(item->decl.signatures.front().text, "template<class T> int convert(T value)"));
    CHECK_FALSE(contains(item->decl.signatures.front().text, "template <"));
    REQUIRE(item->descr.elements.size() == 1);
    CHECK(item->descr.elements.front().kind == ir::ElementKind::Effects);
}

TEST_CASE("build_document - spec_attach.hpp attaches itemdecl/itemdescr to each out-of-line overload") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());
    const ir::Document& document = built->document;

    const ir::Section* access = nullptr;
    for (const ir::Node& node : document.nodes) {
        if (const auto* section = std::get_if<ir::Section>(&node);
            section != nullptr && section->stable_name == "stack.access") {
            access = section;
            break;
        }
    }
    REQUIRE(access != nullptr);
    REQUIRE(access->children.size() == 3);

    const auto* top_nonconst = std::get_if<ir::SpecItem>(&access->children[0]);
    const auto* top_const    = std::get_if<ir::SpecItem>(&access->children[1]);
    const auto* push         = std::get_if<ir::SpecItem>(&access->children[2]);
    REQUIRE(top_nonconst != nullptr);
    REQUIRE(top_const != nullptr);
    REQUIRE(push != nullptr);

    // Each SpecItem carries exactly one signature: the unqualified in-class
    // declaration, not the out-of-line `stack::` form.
    REQUIRE(top_nonconst->decl.signatures.size() == 1);
    REQUIRE(top_const->decl.signatures.size() == 1);
    REQUIRE(push->decl.signatures.size() == 1);

    const std::string& top_nonconst_text = top_nonconst->decl.signatures[0].text;
    const std::string& top_const_text    = top_const->decl.signatures[0].text;
    const std::string& push_text         = push->decl.signatures[0].text;

    CHECK(contains(top_nonconst_text, "top()"));
    CHECK_FALSE(contains(top_nonconst_text, "top() const"));
    CHECK_FALSE(contains(top_nonconst_text, "stack::"));

    CHECK(contains(top_const_text, "top() const"));
    CHECK_FALSE(contains(top_const_text, "stack::"));

    CHECK(contains(push_text, "push(int value)"));
    CHECK_FALSE(contains(push_text, "stack::"));

    // Descr: each definition's own docblock, lowered to the expected element.
    REQUIRE(top_nonconst->descr.elements.size() == 1);
    CHECK(top_nonconst->descr.elements[0].kind == ir::ElementKind::Returns);

    REQUIRE(top_const->descr.elements.size() == 1);
    CHECK(top_const->descr.elements[0].kind == ir::ElementKind::Returns);

    REQUIRE(push->descr.elements.size() == 1);
    CHECK(push->descr.elements[0].kind == ir::ElementKind::Effects);

    const auto paragraph_text = [](const ir::Paragraph& para) {
        std::string out;
        for (const ir::Inline& in : para) {
            if (const auto* text = std::get_if<ir::TextInline>(&in))
                out += text->text;
            else if (const auto* code = std::get_if<ir::CodeInline>(&in))
                out += code->code.text;
        }
        return out;
    };

    REQUIRE(top_nonconst->descr.elements[0].paragraphs.size() == 1);
    CHECK(contains(paragraph_text(top_nonconst->descr.elements[0].paragraphs[0]), "A reference to the top element."));

    REQUIRE(top_const->descr.elements[0].paragraphs.size() == 1);
    CHECK(contains(paragraph_text(top_const->descr.elements[0].paragraphs[0]), "A reference to the top element."));

    REQUIRE(push->descr.elements[0].paragraphs.size() == 1);
    CHECK(contains(paragraph_text(push->descr.elements[0].paragraphs[0]), "Adds"));
    CHECK(contains(paragraph_text(push->descr.elements[0].paragraphs[0]), "to the stack."));
}
