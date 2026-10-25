// tests/beman/specgen/frontend/document_files.test.cpp           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/document_files.hpp>
#include <beman/specgen/frontend/document_files.hpp> // Re-inclusion verification

#include <beman/specgen/frontend/parse_header.hpp>

#include <clang/AST/DeclBase.h>
#include <clang/Basic/SourceManager.h>

#include <catch2/catch_test_macros.hpp>

#include <ranges>
#include <string>
#include <vector>

namespace frontend = beman::specgen::frontend;

TEST_CASE("document_files - a one-file document is its main file, at its own offsets") {
    const std::string            header = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_sample.hpp";
    const frontend::ParsedHeader parsed = frontend::parse_header(header, {});
    REQUIRE(parsed.ast != nullptr);
    const clang::SourceManager&   sm  = parsed.ast->getSourceManager();
    const frontend::DocumentFiles doc = frontend::DocumentFiles::discover(*parsed.ast, sm);
    CHECK(doc.main() == sm.getMainFileID());
    CHECK(doc.parts().empty());
    CHECK(doc.offset_in(doc.main(), 42) == 42);

    std::vector<clang::Decl*> decls;
    // substrate generic algorithm: drives collect_top_level_decl's
    // side effect, as its production caller does.
    for (clang::Decl* decl : std::ranges::subrange(parsed.ast->top_level_begin(), parsed.ast->top_level_end()))
        frontend::collect_top_level_decl(decl, sm, doc, decls);
    CHECK_FALSE(decls.empty());
}
