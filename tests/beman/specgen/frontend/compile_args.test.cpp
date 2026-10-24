// tests/beman/specgen/frontend/compile_args.test.cpp               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/compile_args.hpp>
#include <beman/specgen/frontend/compile_args.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace frontend = beman::specgen::frontend;

// filter_compile_command_args is pure string manipulation with no
// Clang dependency of its own (see its doc comment in compile_args.hpp), so
// it is exercised directly here rather than through a real compilation
// database.

TEST_CASE("compile_args - filter_compile_command_args drops argv[0], the input "
          "filename, and -c") {
    const std::vector<std::string> command_line = {"/usr/bin/c++", "-DFOO", "-I/inc", "-c", "/path/to/file.cpp"};
    const std::vector<std::string> expected     = {"-DFOO", "-I/inc"};
    CHECK(frontend::filter_compile_command_args(command_line, "/path/to/file.cpp") == expected);
}

TEST_CASE("compile_args - filter_compile_command_args drops -o and its argument") {
    const std::vector<std::string> command_line = {
        "/usr/bin/c++", "-I/inc", "-o", "file.o", "-c", "/path/to/file.cpp"};
    const std::vector<std::string> expected = {"-I/inc"};
    CHECK(frontend::filter_compile_command_args(command_line, "/path/to/file.cpp") == expected);
}

TEST_CASE("compile_args - filter_compile_command_args drops a bare -- (the "
          "inferMissingCompileCommands sentinel)") {
    // Design coordination §7 assumed a database entry always looks like a
    // real compiler invocation (argv[0] ... -c -o <file> <input>). It does
    // not always: clang::tooling::CompilationDatabase transparently infers a
    // command for a file with no literal entry, and that inferred command
    // ends in a bare "--" before the filename rather than "-c"/"-o" — see the
    // function's own doc comment in compile_args.hpp for the mechanism and why
    // leaving this in corrupts the parse (a trailing "--" makes clang read
    // the next argument as a positional file rather than an option).
    const std::vector<std::string> command_line = {"/usr/bin/c++", "-I/inc", "--", "/path/to/file.cpp"};
    const std::vector<std::string> expected     = {"-I/inc"};
    CHECK(frontend::filter_compile_command_args(command_line, "/path/to/file.cpp") == expected);
}

TEST_CASE("compile_args - filter_compile_command_args passes an -std through "
          "unchanged") {
    // A project's own -std overrides specgen's default because it is
    // spliced in *before* the two structural flags, not because this
    // function treats it specially — it is ordinary content to this filter.
    const std::vector<std::string> command_line = {"/usr/bin/c++", "-std=c++23", "/path/to/file.cpp"};
    const std::vector<std::string> expected     = {"-std=c++23"};
    CHECK(frontend::filter_compile_command_args(command_line, "/path/to/file.cpp") == expected);
}

TEST_CASE("compile_args - filter_compile_command_args on an empty command line") {
    CHECK(frontend::filter_compile_command_args({}, "/path/to/file.cpp").empty());
}

// resolve_extra_args: the first source that contributes wins outright.

TEST_CASE("compile_args - resolve_extra_args takes extra_args verbatim and names no source") {
    const frontend::ParseOptions options{.extra_args             = {"-I/inc", "-DX"},
                                         .compile_commands_dir   = "/nonexistent/nowhere",
                                         .probe_compile_commands = true};
    const auto                   resolved = frontend::resolve_extra_args("/path/to/header.hpp", options);
    CHECK(resolved.args == std::vector<std::string>{"-I/inc", "-DX"});
    CHECK(resolved.source.empty());
}

TEST_CASE("compile_args - resolve_extra_args with no source contributes nothing") {
    const auto resolved = frontend::resolve_extra_args("/path/to/header.hpp", {});
    CHECK(resolved.args.empty());
    CHECK(resolved.source.empty());
}

TEST_CASE("compile_args - resolve_extra_args reads a compilation database's entry for the header") {
    // A database in a scratch directory with one entry for the header; its
    // command is filtered, and the entry's own directory is the source.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "specgen-compile-args-test";
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "compile_commands.json") << R"([{
        "directory": "/work",
        "command": "/opt/bin/compiler -I/inc -DX -c /work/header.hpp -o header.o",
        "file": "/work/header.hpp"
    }])";

    const auto resolved =
        frontend::resolve_extra_args("/work/header.hpp", {.extra_args = {}, .compile_commands_dir = dir.string()});
    CHECK(resolved.args == std::vector<std::string>{"-I/inc", "-DX"});
    CHECK(resolved.source == "/work");
    std::filesystem::remove_all(dir);
}
