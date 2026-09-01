// tests/beman/specgen/frontend/collect.test.cpp                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// collect_interleaved() over the hand-curated corpus header
// (decision hermetic-corpus, tests/corpus/spec_sample.hpp) produces the
// decl/comment interleave design §3.2 describes. The header's SPDX/license
// comment and trailing brace/`#endif` comments are real items too
// (-fparse-all-comments captures ordinary comments, not just docblocks), so
// assertions below key off content and relative order rather than assuming
// the first item is the class.

#include <beman/specgen/frontend/frontend.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

namespace frontend = beman::specgen::frontend;

namespace {

const std::string kCorpusHeader = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_sample.hpp";

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

} // namespace

TEST_CASE("collect_interleaved - main-file declarations are the class then its out-of-line member") {
    const auto items = frontend::collect_interleaved(kCorpusHeader).items;
    REQUIRE_FALSE(items.empty());

    std::vector<frontend::SourceItem> decls;
    std::copy_if(items.begin(), items.end(), std::back_inserter(decls), [](const frontend::SourceItem& item) {
        return item.kind == frontend::SourceItem::Kind::Declaration;
    });

    // Only the class and the out-of-line definition are top-level; the
    // in-class member declaration is not (design §3.1/§3.2).
    REQUIRE(decls.size() == 2);
    CHECK(contains(decls[0].label, "sample"));
    CHECK(contains(decls[1].label, "observe"));
    CHECK(decls[0].offset < decls[1].offset);
}

TEST_CASE("collect_interleaved - draft-form and section comments interleave with the decls in source order") {
    const auto items = frontend::collect_interleaved(kCorpusHeader).items;

    const auto find_containing = [&items](const char* needle) {
        return std::find_if(items.begin(), items.end(), [needle](const frontend::SourceItem& item) {
            return item.kind == frontend::SourceItem::Kind::Comment && item.label.find(needle) != std::string::npos;
        });
    };
    const auto find_decl_containing = [&items](const char* needle) {
        return std::find_if(items.begin(), items.end(), [needle](const frontend::SourceItem& item) {
            return item.kind == frontend::SourceItem::Kind::Declaration &&
                   item.label.find(needle) != std::string::npos;
        });
    };

    const auto class_decl  = find_decl_containing("sample");
    const auto member_ref  = find_containing("\\ref{sample.observers}");
    const auto section     = find_containing("\\rSec3[sample.observers]");
    const auto docblock    = find_containing("\\effects");
    const auto out_of_line = find_decl_containing("observe");

    REQUIRE(class_decl != items.end());
    REQUIRE(member_ref != items.end());
    REQUIRE(section != items.end());
    REQUIRE(docblock != items.end());
    REQUIRE(out_of_line != items.end());

    // The draft \ref group comment lives inside the class body: after the
    // class opens, before the section header that starts the definition
    // region — i.e. it precedes the class's closing brace.
    CHECK(class_decl->offset < member_ref->offset);
    CHECK(member_ref->offset < section->offset);

    // The //! docblock sits between the \rSec3 section header and the
    // out-of-line definition it documents.
    CHECK(section->offset < docblock->offset);
    CHECK(docblock->offset < out_of_line->offset);
}

TEST_CASE("collect_interleaved - the docblock merges its consecutive //! lines into one comment") {
    const auto items    = frontend::collect_interleaved(kCorpusHeader).items;
    const auto docblock = std::find_if(items.begin(), items.end(), [](const frontend::SourceItem& item) {
        return item.kind == frontend::SourceItem::Kind::Comment && item.label.find("\\effects") != std::string::npos;
    });
    REQUIRE(docblock != items.end());
    CHECK(contains(docblock->label, "\\returns"));
}

TEST_CASE("collect_interleaved - an unreadable path yields an empty result") {
    const auto result = frontend::collect_interleaved("tests/corpus/does-not-exist.hpp");
    CHECK(result.items.empty());
    CHECK_FALSE(result.had_parse_error);
}

// --- a parse Clang could not finish still yields an interleave --------------

TEST_CASE("collect_interleaved - a header Clang could not fully parse still returns its (partial) items") {
    // The include-path fixture, reused with its include deliberately unsatisfied
    // (no `-I` supplied here): buildASTFromCodeWithArgs recovers from the
    // fatal preprocessor error and hands back a non-null, partial AST, so
    // this is the collect_interleaved side of the same distinction
    // build_document's BuildFailure test (document.test.cpp) makes on the
    // build_document side — dump-decls keeps running on it rather than
    // failing, which is the property this test pins.
    const std::string include_path_header =
        std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/include_path/consumer/spec_include.hpp";
    const auto result = frontend::collect_interleaved(include_path_header);
    CHECK(result.had_parse_error);
    CHECK_FALSE(result.items.empty());
}
