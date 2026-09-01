// tests/beman/specgen/frontend/document.test.cpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// build_document() over the hand-curated corpus header
// (decision hermetic-corpus, tests/corpus/spec_widget.hpp) folds the
// decl/comment interleave into the ir::Document section skeleton design §3.2
// describes — `\rSec` comments become nested ir::Section siblings, and the
// class and its out-of-line member definitions hang off the right frame in
// source order. Code text is other files' business (synopsis.test.cpp,
// template.test.cpp): this file pins the skeleton, not the bytes.

#include <beman/specgen/frontend/frontend.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/validate/validate.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace frontend = beman::specgen::frontend;
namespace ir       = beman::specgen::ir;

namespace {

const std::string kCorpusHeader = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_widget.hpp";

}

TEST_CASE("build_document - spec_widget.hpp yields the class synopsis plus two \\rSec3 sections") {
    const auto built = frontend::build_document(kCorpusHeader);
    REQUIRE(built.has_value());
    const ir::Document& document = built->document;
    REQUIRE(document.nodes.size() == 3);
    CHECK(built->diagnostics.empty());

    CHECK(std::holds_alternative<ir::Synopsis>(document.nodes[0]));

    const auto* cons = std::get_if<ir::Section>(&document.nodes[1]);
    REQUIRE(cons != nullptr);
    CHECK(cons->stable_name == "widget.cons");
    CHECK(cons->title == "Constructors");
    REQUIRE(cons->children.size() == 2);
    CHECK(std::holds_alternative<ir::SpecItem>(cons->children[0]));
    CHECK(std::holds_alternative<ir::SpecItem>(cons->children[1]));

    const auto* observers = std::get_if<ir::Section>(&document.nodes[2]);
    REQUIRE(observers != nullptr);
    CHECK(observers->stable_name == "widget.observers");
    CHECK(observers->title == "Observers");
    REQUIRE(observers->children.size() == 1);
    CHECK(std::holds_alternative<ir::SpecItem>(observers->children[0]));
}

TEST_CASE("build_document - an unreadable path yields an unexpected BuildFailure") {
    const auto built = frontend::build_document("tests/corpus/does-not-exist.hpp");
    CHECK_FALSE(built.has_value());
    CHECK_FALSE(built.error().message.empty());
}

TEST_CASE("build_document - a verbatim itemdecl is one exact standalone item") {
    const auto built = frontend::build_document(std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_verbatim_itemdecl.hpp");
    REQUIRE(built.has_value());
    CHECK(built->diagnostics.empty());

    REQUIRE(built->document.nodes.size() == 1);
    const auto* section = std::get_if<ir::Section>(&built->document.nodes.front());
    REQUIRE(section != nullptr);
    REQUIRE(section->children.size() == 1);
    const auto* item = std::get_if<ir::SpecItem>(&section->children.front());
    REQUIRE(item != nullptr);

    REQUIRE(item->decl.signatures.size() == 1);
    const ir::CodeText& signature = item->decl.signatures.front();
    CHECK(signature.text == "struct nullopt_t {@\\seebelow@};\n\n"
                            "inline constexpr nullopt_t nullopt(@\\unspec@);");
    CHECK(signature.spans.empty());
    CHECK(item->decl.index.empty());
    REQUIRE(item->descr.elements.size() == 1);
    CHECK(item->descr.elements.front().kind == ir::ElementKind::Remarks);
}

TEST_CASE("build_document - a merged record leaves only its standalone verbatim item") {
    const auto built =
        frontend::build_document(std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_verbatim_record_merge.hpp");
    REQUIRE(built.has_value());
    CHECK(built->diagnostics.empty());
    CHECK(beman::specgen::validate::validate(built->document).empty());

    REQUIRE(built->document.nodes.size() == 1);
    const auto* section = std::get_if<ir::Section>(&built->document.nodes.front());
    REQUIRE(section != nullptr);
    CHECK(section->stable_name == "optional.hash");
    REQUIRE(section->children.size() == 1);

    const auto* item = std::get_if<ir::SpecItem>(&section->children.front());
    REQUIRE(item != nullptr);
    REQUIRE(item->decl.signatures.size() == 1);
    CHECK(item->decl.signatures.front().text == "template<class T> struct hash<optional<T>>;");
    REQUIRE(item->descr.elements.size() == 1);
    CHECK(item->descr.elements.front().kind == ir::ElementKind::Remarks);
}

TEST_CASE("build_document - suppressing a record preserves its docblock diagnostics") {
    const auto built = frontend::build_document(std::string(BEMAN_SPECGEN_CORPUS_DIR) +
                                                "/support/spec_record_suppression_diagnostic.hpp");
    REQUIRE(built.has_value());
    CHECK(built->document.nodes.empty());
    REQUIRE(built->diagnostics.size() == 1);
    CHECK(built->diagnostics.front().message.contains("unknown tag \\not-a-specgen-tag"));
}

// --- generate must not turn a half-parsed header into wording --------------

TEST_CASE("build_document - a header Clang could not fully parse is a distinct BuildFailure, not success") {
    // The include-path fixture, reused with its include deliberately unsatisfied (no
    // `-I` supplied here, unlike golden.include_path/.include_path_db):
    // buildASTFromCodeWithArgs recovers from the fatal preprocessor error and
    // hands back a non-null, partial AST, so the "unreadable path" case above
    // cannot exercise this — it needs a real, if partial, parse.
    const std::string include_path_header =
        std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/include_path/consumer/spec_include.hpp";
    const auto built = frontend::build_document(include_path_header);
    REQUIRE_FALSE(built.has_value());
    CHECK_FALSE(built.error().message.empty());
    // Distinct wording from the unreadable-path message above: a reader must
    // be able to tell "could not open/parse this at all" from "parsed it, and
    // Clang reported an error partway through".
    CHECK(built.error().message.find("could not parse it") == std::string::npos);
}

// --- what a body the tool never renders names (design §9) -------------------

TEST_CASE("build_document - spec_optional.hpp records the members its unrendered bodies name") {
    // The front-end half of design §9's note-severity leakage clause: the
    // Tier-A rule (validate.cpp) can ask the roster whether the reader can
    // see a name, but a body with no `\effects-equiv` never becomes wording,
    // so nothing downstream would otherwise know the name was used at all.
    const auto built = frontend::build_document(std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_optional.hpp");
    REQUIRE(built.has_value());
    const std::vector<ir::BodyUse>& uses = built->document.unextracted_uses;

    const auto records = [&uses](std::string_view function, std::string_view member) {
        return std::ranges::any_of(
            uses, [&](const ir::BodyUse& use) { return use.function == function && use.member == member; });
    };

    // The default constructor's body calls the undocumented private helper.
    // A constructor is labelled by its class rather than by its own name,
    // which for a class template would carry the parameter list.
    CHECK(records("optional::optional", "hard_reset"));

    // An `\expos` member named by another unrendered body is recorded just
    // the same: whether the reader can see it is the validator's question,
    // not the collector's.
    CHECK(records("optional::has_value", "engaged_"));

    // `value_or` carries `\effects-equiv`, so its body *is* wording -- a
    // hidden name there is design §9's error case, reported off the rendered
    // text instead. It must not appear here.
    CHECK_FALSE(
        std::ranges::any_of(uses, [](const ir::BodyUse& use) { return use.function == "optional::value_or"; }));

    // Sorted and deduplicated, so the emitted IR does not depend on the order
    // the AST walk reached things.
    CHECK(
        std::ranges::is_sorted(uses, {}, [](const ir::BodyUse& use) { return std::pair{use.function, use.member}; }));
}
