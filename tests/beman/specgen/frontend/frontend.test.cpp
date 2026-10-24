// tests/beman/specgen/frontend/frontend.test.cpp                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Smoke test for the Clang front-end tier. It asserts only that the two
// LLVM surfaces the tier depends on — the tooling parser and clang::format —
// link and run. Real extraction coverage lives in the other test files here.

#include <beman/specgen/frontend/frontend.hpp>

#include <catch2/catch_test_case_info.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>
#include <string>

namespace frontend = beman::specgen::frontend;

TEST_CASE("frontend - valid C++ parses into an AST") {
    const auto result = frontend::smoke_check("int f(int x) { return x; }\n");
    CHECK(result.ast_built);
}

TEST_CASE("frontend - clang-format normalizes the fixed snippet") {
    const auto result = frontend::smoke_check("int g();\n");
    // getLLVMStyle() collapses the doubled spaces and tightens the braces; we do
    // not pin the exact bytes here (the goldens own the FormatStyle), only that
    // the formatter ran and returned tightened output.
    CHECK_FALSE(result.formatted.empty());
    CHECK(result.formatted.find("  ") == std::string::npos);
}

TEST_CASE("frontend - the header is self-contained on re-inclusion") {
// Idempotence check (decision component-recipe): including the header twice is
// a no-op, so the guard holds.
#include <beman/specgen/frontend/frontend.hpp>
    SUCCEED();
}

// build_document_with_sources reports the files the parse read (frontend.hpp),
// which is what a Makefile dependency fragment's prerequisite list is made of.
// Checked here rather than only through the driver's `--depfile` golden,
// because what it rests on is a Clang API contract -- that an `#include`d file
// leaves a FileEntry behind while the in-memory main file does not -- and a
// golden diffing a whole fragment would report a Clang upgrade that broke that
// as "the depfile moved".

TEST_CASE("frontend - build_document_with_sources leads with the header itself") {
    const std::string header = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_optional.hpp";
    const auto        built  = frontend::build_document_with_sources(header);
    REQUIRE(built.has_value());
    REQUIRE_FALSE(built->sources.empty());
    CHECK(built->sources.front() == header);
}

TEST_CASE("frontend - build_document_with_sources reports an included header") {
    // spec_foreign_include.hpp includes support/spec_foreign_detail.hpp, which
    // is a real file on disk and so does leave a FileEntry behind.
    const std::string header = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_foreign_include.hpp";
    const auto        built  = frontend::build_document_with_sources(header);
    REQUIRE(built.has_value());
    CHECK(built->sources.front() == header);
    const auto names_detail = [](const std::string& path) {
        return path.find("spec_foreign_detail.hpp") != std::string::npos;
    };
    CHECK(std::ranges::any_of(built->sources, names_detail));
    // Sorted past the leading main file: an unsorted list would make two runs
    // over one unchanged header produce two different fragments.
    CHECK(std::ranges::is_sorted(built->sources | std::views::drop(1)));
}

TEST_CASE("frontend - build_document_with_sources omits system headers") {
    // -MMD semantics, not -MD: naming every libstdc++ path would bury the one
    // header a reader is scanning the fragment for.
    const std::string header = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_optional.hpp";
    const auto        built  = frontend::build_document_with_sources(header);
    REQUIRE(built.has_value());
    const auto is_system = [](const std::string& path) { return path.starts_with("/usr/"); };
    CHECK(std::ranges::none_of(built->sources, is_system));
}

TEST_CASE("frontend - build_document_with_sources fails on a header it cannot read") {
    CHECK_FALSE(frontend::build_document_with_sources("/nonexistent/nowhere.hpp").has_value());
}
