// tests/beman/specgen/frontend/directive_sets.test.cpp           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/directive_sets.hpp>
#include <beman/specgen/frontend/directive_sets.hpp> // Re-inclusion verification

#include <beman/specgen/document_build.hpp>
#include <beman/specgen/frontend/document_files.hpp>
#include <beman/specgen/frontend/parse_header.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/Frontend/ASTUnit.h>
#include <llvm/Support/Casting.h>

#include <catch2/catch_test_macros.hpp>

#include <ranges>
#include <set>
#include <string>
#include <vector>

namespace frontend = beman::specgen::frontend;

// Covered end to end by the build_document scenario tests in this
// directory; this file holds the header's own checks and the namespace
// projections' (issue #209), which have answers worth stating directly.
TEST_CASE("directive_sets - the header compiles on its own") { SUCCEED(); }

namespace {

struct ParsedCorpus {
    frontend::ParsedHeader    parsed;
    std::vector<clang::Decl*> decls;

    explicit ParsedCorpus(const std::string& name)
        : parsed(frontend::parse_header(std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/" + name, {})) {
        REQUIRE(parsed.ast != nullptr);
        const clang::SourceManager&   sm  = parsed.ast->getSourceManager();
        const frontend::DocumentFiles doc = frontend::DocumentFiles::discover(*parsed.ast, sm);
        for (clang::Decl* decl : std::ranges::subrange(parsed.ast->top_level_begin(), parsed.ast->top_level_end()))
            frontend::collect_top_level_decl(decl, sm, doc, decls);
    }

    std::vector<const clang::NamedDecl*> named(const std::string& name) const {
        return decls |
               std::views::transform([](const clang::Decl* decl) { return llvm::dyn_cast<clang::NamedDecl>(decl); }) |
               std::views::filter(
                   [&](const clang::NamedDecl* decl) { return decl != nullptr && decl->getNameAsString() == name; }) |
               std::ranges::to<std::vector>();
    }
};

} // namespace

TEST_CASE("directive_sets - build_specified_namespace_set reads the marker off any redeclaration") {
    ParsedCorpus                        corpus("spec_nested_namespace.hpp");
    const frontend::SpecifiedNamespaces specified = frontend::build_specified_namespace_set(
        corpus.decls, corpus.parsed.ast->getASTContext(), corpus.parsed.ast->getSourceManager());
    CHECK(specified.paths == std::set<std::string>{"demo::proto", "demo::views"});
    CHECK(specified.diagnostics.empty());
}

TEST_CASE("directive_sets - rendered_namespace is the specified path below the dropped prefix") {
    ParsedCorpus                corpus("spec_nested_namespace.hpp");
    const std::set<std::string> drop = frontend::build_namespace_drop_set(corpus.decls);
    const std::set<std::string> specified =
        frontend::build_specified_namespace_set(
            corpus.decls, corpus.parsed.ast->getASTContext(), corpus.parsed.ast->getSourceManager())
            .paths;

    const auto adapt = corpus.named("adapt");
    REQUIRE_FALSE(adapt.empty());
    CHECK(frontend::rendered_namespace(adapt.front(), drop, specified).qualifier == "views");
    CHECK(frontend::rendered_namespace(adapt.front(), drop, specified).definition == "views");
    // An unmarked namespace flattens, exactly as before.
    const auto adapt_fn = corpus.named("adapt_fn");
    REQUIRE_FALSE(adapt_fn.empty());
    CHECK(frontend::rendered_namespace(adapt_fn.front(), drop, specified).qualifier.empty());
    // `std`-rooted needs no marker.
    const auto borrowed = corpus.named("enable_borrowed_range");
    REQUIRE_FALSE(borrowed.empty());
    CHECK(frontend::rendered_namespace(borrowed.front(), drop, specified).qualifier == "ranges");
}

TEST_CASE("directive_sets - declarator_qualifier qualifies functions and variables, never types") {
    ParsedCorpus                corpus("spec_nested_namespace.hpp");
    const std::set<std::string> drop = frontend::build_namespace_drop_set(corpus.decls);
    const std::set<std::string> specified =
        frontend::build_specified_namespace_set(
            corpus.decls, corpus.parsed.ast->getASTContext(), corpus.parsed.ast->getSourceManager())
            .paths;

    const std::set<std::string> decode_qualifiers = corpus.named("decode") |
                                                    std::views::transform([&](const clang::NamedDecl* decl) {
                                                        return frontend::declarator_qualifier(decl, drop, specified);
                                                    }) |
                                                    std::ranges::to<std::set<std::string>>();
    CHECK(decode_qualifiers == std::set<std::string>{"", "proto"});
    const auto adapt = corpus.named("adapt");
    REQUIRE_FALSE(adapt.empty());
    CHECK(frontend::declarator_qualifier(adapt.front(), drop, specified) == "views");
    const auto encoding = corpus.named("encoding");
    REQUIRE_FALSE(encoding.empty());
    CHECK(frontend::declarator_qualifier(encoding.front(), drop, specified).empty());
    const auto decoder = corpus.named("decoder");
    REQUIRE_FALSE(decoder.empty());
    CHECK(frontend::declarator_qualifier(decoder.front(), drop, specified).empty());
}

TEST_CASE("directive_sets - a misplaced \\specified is an Error, in file order") {
    ParsedCorpus                        corpus("spec_specified_diagnostics.hpp");
    const frontend::SpecifiedNamespaces specified = frontend::build_specified_namespace_set(
        corpus.decls, corpus.parsed.ast->getASTContext(), corpus.parsed.ast->getSourceManager());
    // The two namespaces are marked even though their docblocks carry more:
    // the marker takes effect and the extra is reported.
    CHECK(specified.paths == std::set<std::string>{"demo::detail", "demo::views"});
    REQUIRE(specified.diagnostics.size() == 3);
    CHECK(specified.diagnostics[0].message == "\\specified applies only to a namespace");
    CHECK(specified.diagnostics[1].message.starts_with("a namespace accepts only \\specified"));
    CHECK(specified.diagnostics[2].message.starts_with("a namespace accepts only \\specified"));
    CHECK(specified.diagnostics[0].line < specified.diagnostics[1].line);
    CHECK(specified.diagnostics[1].line < specified.diagnostics[2].line);
}
