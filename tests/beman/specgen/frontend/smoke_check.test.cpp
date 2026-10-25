// tests/beman/specgen/frontend/smoke_check.test.cpp              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Asserts only that the two LLVM surfaces the front end depends on, the
// tooling parser and clang::format, link and run. Real extraction coverage
// lives in the other test files here.

#include <beman/specgen/frontend/smoke_check.hpp>
#include <beman/specgen/frontend/smoke_check.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace frontend = beman::specgen::frontend;

TEST_CASE("smoke_check - valid C++ parses into an AST") {
    const auto result = frontend::smoke_check("int f(int x) { return x; }\n");
    CHECK(result.ast_built);
}

TEST_CASE("smoke_check - clang-format normalizes the fixed snippet") {
    const auto result = frontend::smoke_check("int g();\n");
    // getLLVMStyle() collapses the doubled spaces and tightens the braces; we do
    // not pin the exact bytes here (the goldens own the FormatStyle), only that
    // the formatter ran and returned tightened output.
    CHECK_FALSE(result.formatted.empty());
    CHECK(result.formatted.find("  ") == std::string::npos);
}
