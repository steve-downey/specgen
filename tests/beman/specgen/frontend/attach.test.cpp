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
const std::string kMergeDeletedTemplateHeader =
    std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_merge_deleted_template.hpp";
const std::string kDeletedTailSpellingHeader =
    std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_deleted_tail_spelling.hpp";

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

// A `\merge`d deleted function *template* used to leave a dangling
// `= delete;` fragment behind: Clang's own parser never extends the
// templated FunctionDecl's recorded end past the keyword the way it does
// for a plain FunctionDecl, so trusting that end location stopped the
// removal at the declarator's closing `)` (issue #85).
TEST_CASE("build_document - a merged deleted function template is removed whole, not left as a `= delete;` stub") {
    const auto built = frontend::build_document(kMergeDeletedTemplateHeader);
    REQUIRE(built.has_value());
    CHECK(built->diagnostics.empty());

    const auto section = std::ranges::find_if(built->document.nodes, [](const ir::Node& node) {
        const auto* found = std::get_if<ir::Section>(&node);
        return found != nullptr && found->stable_name == "demo.widget";
    });
    REQUIRE(section != built->document.nodes.end());
    const ir::Section& widget_section = std::get<ir::Section>(*section);
    const auto         synopsis       = std::ranges::find_if(
        widget_section.children, [](const ir::Node& node) { return std::holds_alternative<ir::Synopsis>(node); });
    REQUIRE(synopsis != widget_section.children.end());
    const ir::Synopsis& syn = std::get<ir::Synopsis>(*synopsis);
    CHECK_FALSE(contains(syn.code.text, "delete"));
    CHECK_FALSE(contains(syn.code.text, "template <class G>"));
    CHECK(contains(syn.code.text, "constexpr explicit widget(int g);"));
}

// The same removal, for the two spellings of the tail that reading the
// source text cannot recognize (issue #99): one written through a macro,
// where raw-lexing forward finds the macro's own identifier instead of a
// keyword, and P2573's `= delete("reason")`, where the keyword is found but
// the parenthesized message sits past it and used to be left behind on its
// own. Both are asked of the AST now, so only the tail's extent is lexed.
TEST_CASE("build_document - a merged deleted member goes whole however its `= delete` tail is spelled") {
    const auto built = frontend::build_document(kDeletedTailSpellingHeader);
    REQUIRE(built.has_value());
    CHECK(built->diagnostics.empty());

    const auto section = std::ranges::find_if(built->document.nodes, [](const ir::Node& node) {
        const auto* found = std::get_if<ir::Section>(&node);
        return found != nullptr && found->stable_name == "demo.gauge";
    });
    REQUIRE(section != built->document.nodes.end());
    const ir::Section& gauge_section = std::get<ir::Section>(*section);
    const auto         synopsis      = std::ranges::find_if(
        gauge_section.children, [](const ir::Node& node) { return std::holds_alternative<ir::Synopsis>(node); });
    REQUIRE(synopsis != gauge_section.children.end());
    const ir::Synopsis& syn = std::get<ir::Synopsis>(*synopsis);

    // Nothing of any merged declaration survives: not the macro's name, not
    // the message that would have been stranded behind the keyword, and not
    // the `= ` that introduced either.
    CHECK_FALSE(contains(syn.code.text, "BEMAN_SPECGEN_CORPUS_DELETE_MSG"));
    CHECK_FALSE(contains(syn.code.text, "BEMAN_SPECGEN_CORPUS_DEFAULT"));
    CHECK_FALSE(contains(syn.code.text, "only int is accepted"));
    CHECK_FALSE(contains(syn.code.text, "gauge(double)"));
    CHECK_FALSE(contains(syn.code.text, "gauge(const gauge&)"));

    // The literal spellings render exactly as they did before, and the
    // `\freestanding-deleted` suffix lands past the whole tail rather than
    // between the keyword and its message.
    CHECK(contains(syn.code.text, "constexpr gauge() = default;"));
    CHECK(contains(syn.code.text, "constexpr gauge(gauge&&) = delete;"));
    CHECK(contains(syn.code.text, "\"gauge: not assignable\"); // freestanding-deleted"));
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
