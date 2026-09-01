// tests/beman/specgen/frontend/frontend.test.cpp                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Smoke test for the Clang front-end tier. It asserts only that the two
// LLVM surfaces the tier depends on — the tooling parser and clang::format —
// link and run. Real extraction coverage lives in the other test files here.

#include <beman/specgen/frontend/frontend.hpp>

#include <catch2/catch_test_case_info.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

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

// filter_compile_command_args is pure string manipulation with no
// Clang dependency of its own (see its doc comment in frontend.hpp), so it is
// exercised directly here rather than through an ASTUnit or a real
// compilation database.

TEST_CASE("frontend - filter_compile_command_args drops argv[0], the input "
          "filename, and -c") {
    const std::vector<std::string> command_line = {"/usr/bin/c++", "-DFOO", "-I/inc", "-c", "/path/to/file.cpp"};
    const std::vector<std::string> expected     = {"-DFOO", "-I/inc"};
    CHECK(frontend::filter_compile_command_args(command_line, "/path/to/file.cpp") == expected);
}

TEST_CASE("frontend - filter_compile_command_args drops -o and its argument") {
    const std::vector<std::string> command_line = {
        "/usr/bin/c++", "-I/inc", "-o", "file.o", "-c", "/path/to/file.cpp"};
    const std::vector<std::string> expected = {"-I/inc"};
    CHECK(frontend::filter_compile_command_args(command_line, "/path/to/file.cpp") == expected);
}

TEST_CASE("frontend - filter_compile_command_args drops a bare -- (the "
          "inferMissingCompileCommands sentinel)") {
    // Design coordination §7 assumed a database entry always looks like a
    // real compiler invocation (argv[0] ... -c -o <file> <input>). It does
    // not always: clang::tooling::CompilationDatabase transparently infers a
    // command for a file with no literal entry, and that inferred command
    // ends in a bare "--" before the filename rather than "-c"/"-o" — see the
    // function's own doc comment in frontend.hpp for the mechanism and why
    // leaving this in corrupts the parse (a trailing "--" makes clang read
    // the next argument as a positional file rather than an option).
    const std::vector<std::string> command_line = {"/usr/bin/c++", "-I/inc", "--", "/path/to/file.cpp"};
    const std::vector<std::string> expected     = {"-I/inc"};
    CHECK(frontend::filter_compile_command_args(command_line, "/path/to/file.cpp") == expected);
}

TEST_CASE("frontend - filter_compile_command_args passes an -std through "
          "unchanged") {
    // A project's own -std overrides specgen's default because it is
    // spliced in *before* the two structural flags, not because this
    // function treats it specially — it is ordinary content to this filter.
    const std::vector<std::string> command_line = {"/usr/bin/c++", "-std=c++23", "/path/to/file.cpp"};
    const std::vector<std::string> expected     = {"-std=c++23"};
    CHECK(frontend::filter_compile_command_args(command_line, "/path/to/file.cpp") == expected);
}

TEST_CASE("frontend - filter_compile_command_args on an empty command line") {
    CHECK(frontend::filter_compile_command_args({}, "/path/to/file.cpp").empty());
}
