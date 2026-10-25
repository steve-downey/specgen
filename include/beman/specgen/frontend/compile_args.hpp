// include/beman/specgen/frontend/compile_args.hpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — where the front end's extra clang arguments come from: the
// command line's `--` tail, an explicit compilation database, or one found by
// walking up from the header. Shared by the front end's parse and by the
// driver, which resolves them once so it can say where they came from. The
// interface is plain strings; only resolve_extra_args reaches Clang, to load
// a compilation database.

#ifndef BEMAN_SPECGEN_FRONTEND_COMPILE_ARGS_HPP
#define BEMAN_SPECGEN_FRONTEND_COMPILE_ARGS_HPP

#include <string>
#include <string_view>
#include <vector>

namespace beman::specgen::frontend {

// Where the clang arguments `parse_header` appends between its own
// fixed `-std=c++2c` and its two structural flags come from. Every corpus
// header is self-contained, so none needs one of these; a real header
// (beman/optional/optional.hpp) reaches outside itself and needs an `-I`.
//
// Exactly one of the three fields below ever contributes, chosen by
// `resolve_extra_args`'s precedence: the first non-empty source wins
// outright and the remaining ones are never even consulted, let alone
// blended with it. A caller who spells `extra_args` is not asking to be
// second-guessed by a compilation database, and silently blending two
// sources would make a wrong parse impossible to diagnose.
struct ParseOptions {
    // The `--` tail from the command line, verbatim. Highest precedence: if
    // non-empty, no compilation database is even loaded.
    std::vector<std::string> extra_args;
    // A directory holding a compile_commands.json, loaded explicitly.
    // Consulted only when `extra_args` is empty.
    std::string compile_commands_dir;
    // Walk up from the header looking for a compile_commands.json
    // (clang::tooling::CompilationDatabase::autoDetectFromSource). Consulted
    // only when `extra_args` and `compile_commands_dir` are both empty.
    //
    // Defaults to false, and that default is load-bearing, not arbitrary:
    // this repository keeps a gitignored `compile_commands.json` symlink at
    // its root, pointing into whichever build tree a developer last
    // configured. autoDetectFromSource walks *up* from the source file being
    // parsed, so probing on by default would make every generate-mode
    // golden's parse depend on whether that symlink happens to exist and
    // what it happens to point at — nondeterminism a golden suite must not
    // have, since CI (which has no such file) would then silently disagree
    // with a dev box that does. The *driver* turns this on by default
    // (the driver's default, since that is what makes the acid target parseable at all);
    // the golden harness must keep it off. `NO_VALIDATE`
    // (tests/golden/CMakeLists.txt) is this project's precedent for spelling
    // an opt-out explicitly and greppably rather than leaving it incidental.
    bool probe_compile_commands = false;
};

// One entry of a compilation database's CompileCommand::CommandLine, filtered
// per the extraction rule: the compiler itself (argv[0]) and the input file
// name are positional, not flags, and `-c`/`-o <file>` name *this* database
// entry's own output, not anything the header being parsed here needs —
// everything else (an `-I`, a `-D`, a `-std` a project wants to override
// specgen's own default with) passes through untouched. Pure string
// manipulation with no Clang dependency of its own, so it is unit-testable
// without a compilation database or an AST.
//
// Also drops a bare `--`, and that drop is load-bearing: when
// `probe_compile_commands` finds a real compile_commands.json but the header
// being parsed is *not literally one of its entries* (true of every corpus
// header, always — none of them is ever part of the specgen project's own
// build), `clang::tooling::CompilationDatabase` transparently wraps the
// database with `inferMissingCompileCommands`, which interpolates a command
// from the "nearest" real entry and appends it as `{"--", <filename>}` —
// clang tooling's own convention for "everything after this is positional",
// not a compiler flag. Left in, that bare `--` lands between this tier's own
// resolved arguments and its two trailing structural flags, and clang's
// driver then reads `-fsyntax-only` as a *file name* rather than an option
// (verified: `error: no such file or directory: '-fsyntax-only'`), the
// worst failure mode available — a confusing error instead of either a clean
// parse or a clean fallback to the defaults.
std::vector<std::string> filter_compile_command_args(const std::vector<std::string>& command_line,
                                                     const std::string&              filename);

// The result of resolving ParseOptions' three sources for `header_path`:
// `args` is what `parse_header` (frontend/parse_header.hpp) appends between its own fixed
// `-std=c++2c` and its two structural flags; `source` is the *entry's own*
// working directory (a CompileCommand's `Directory`) when a database
// contributed flags — empty when `extra_args` won outright (a caller who
// spelled flags needs no explanation) and empty when no source contributed
// anything (the caller's fixed defaults apply unchanged, as they always
// have). Ready to print as-is.
//
// It is the entry's directory rather than the path of the
// compile_commands.json that supplied it, and deliberately: those two
// coincide often enough to be tempting and are not the same thing — an
// entry's `"directory"` field names where its command was to be *run*, which
// a database is free to point anywhere, and `autoDetectFromSource` does not
// report where it found the file it loaded. Synthesizing
// `<Directory>/compile_commands.json` would print a path that need not
// exist, which is worse than printing less.
struct ResolvedArgs {
    std::vector<std::string> args;
    std::string              source;
};

// Resolve ParseOptions' precedence for `header_path`: `extra_args` first,
// then `compile_commands_dir` if set, then autodetection if
// `probe_compile_commands` is set, then the caller's fixed defaults if none
// of those contributes anything. Exposed (rather than folded silently into
// `collect_interleaved`/`build_document`) so a caller — the driver — can
// resolve it once, print the "which file they came from" diagnostic once,
// and pass the answer straight back in as `extra_args`, which is guaranteed
// to reproduce the identical parse without a second filesystem probe.
ResolvedArgs resolve_extra_args(std::string_view header_path, const ParseOptions& options);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_COMPILE_ARGS_HPP
