// src/beman/specgen/frontend/compile_args.cpp                      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/compile_args.hpp>

#include <clang/Tooling/CompilationDatabase.h>

#include <algorithm>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

// A corpus header never needs an include path — every one is self-contained
// (decision hermetic-corpus) — but a header outside the corpus (the real
// beman/optional/optional.hpp) can reach outside itself. ParseOptions
// (compile_args.hpp) names the three possible sources; these two functions are
// what parse_header (frontend/parse_header.hpp) and the driver share to turn one into the actual
// argument vector.

std::vector<std::string> filter_compile_command_args(const std::vector<std::string>& command_line,
                                                     const std::string&              filename) {
    // A stateful filter: dropping `-o <file>` has to consume the *next*
    // element too, which a plain views::filter predicate cannot express (it
    // sees one element at a time, never two). This is a fold building the
    // surviving vector, threading "skip the next token" as the one bit of
    // state the shape needs — every read is still at the current position,
    // so this is not the scatter the zip/index rule reserves a raw loop for.
    struct State {
        std::vector<std::string> out;
        bool                     skip_next = false;
    };
    // element 0 is the compiler itself (argv[0]), dropped by starting the
    // fold past it rather than by a branch inside the step function.
    State result = std::ranges::fold_left(command_line | std::views::drop(command_line.empty() ? 0 : 1),
                                          State{},
                                          [&](State state, const std::string& arg) {
                                              if (state.skip_next) {
                                                  state.skip_next = false;
                                                  return state;
                                              }
                                              // A bare "--" is not a flag: it is clang tooling's own
                                              // end-of-options sentinel, which `inferMissingCompileCommands`
                                              // appends (as `{"--", filename}`) when a database is asked about
                                              // a file it has no literal entry for -- see this function's doc
                                              // comment for why leaving it in corrupts the parse rather than
                                              // merely doing nothing.
                                              if (arg == filename || arg == "-c" || arg == "--")
                                                  return state;
                                              if (arg == "-o") {
                                                  state.skip_next = true;
                                                  return state;
                                              }
                                              state.out.push_back(arg);
                                              return state;
                                          });
    return std::move(result.out);
}

ResolvedArgs resolve_extra_args(std::string_view header_path, const ParseOptions& options) {
    // A caller who spelled flags is not asking to be second-guessed.
    if (!options.extra_args.empty())
        return ResolvedArgs{options.extra_args, {}};

    // At most one of these two is even attempted, and the order is
    // precedence, not preference — an explicit directory beats autodetection
    // whenever both happen to be set at once.
    std::unique_ptr<clang::tooling::CompilationDatabase> db;
    std::string                                          error;
    if (!options.compile_commands_dir.empty())
        db = clang::tooling::CompilationDatabase::loadFromDirectory(options.compile_commands_dir, error);
    else if (options.probe_compile_commands)
        db = clang::tooling::CompilationDatabase::autoDetectFromSource(std::string(header_path), error);

    if (!db)
        return {};

    // The first entry for this file; no entry is not an error — fall
    // through to the caller's fixed defaults, the same as no database at all.
    const std::vector<clang::tooling::CompileCommand> commands = db->getCompileCommands(std::string(header_path));
    if (commands.empty())
        return {};

    const clang::tooling::CompileCommand& command = commands.front();
    return ResolvedArgs{filter_compile_command_args(command.CommandLine, command.Filename), command.Directory};
}

} // namespace beman::specgen::frontend
