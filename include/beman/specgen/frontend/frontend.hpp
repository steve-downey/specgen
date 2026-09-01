// include/beman/specgen/frontend/frontend.hpp                      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef BEMAN_SPECGEN_FRONTEND_FRONTEND_HPP
#define BEMAN_SPECGEN_FRONTEND_FRONTEND_HPP

// Tier B — the Clang front end. See design plan §3 and decisions ir-boundary
// and llvm-toolchain-pin. Built in every configuration, against the pinned
// LLVM/Clang version. Its sole contract with the clang-free core is
// ir::Document; everything that
// touches ClangTool, the Lexer, Sema, or clang::format lives behind this seam
// and nowhere else, which keeps the LLVM version dependency a single
// replaceable target.
//
// smoke_check is a link-proving smoke check; the other entry points are the
// real ClangTool decl-collection and extraction path.

#include <beman/specgen/document_build.hpp>
#include <beman/specgen/foundation/parse/parser.hpp>
#include <beman/specgen/ir.hpp>

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace beman::specgen::frontend {

// Result of smoke_check: whether Clang parsed the source into an AST,
// and the text clang::format produced for a fixed snippet. Exercising both
// surfaces is the point — the tooling front end (design §3.1) and the
// formatting normalizer (design §3.6) are the two LLVM dependencies the tier
// rests on, so the smoke test asserts both link and run.
struct SmokeResult {
    bool        ast_built = false;
    std::string formatted;
};

// Parse `source` as C++26 into a Clang AST and reformat a fixed snippet through
// clang-format's LLVM style. Proves the Tier B toolchain is wired end to end;
// carries no wording-generation logic yet.
SmokeResult smoke_check(std::string_view source);

// One entry in the decl/comment interleave (design §3.2): either a main-file
// top-level declaration or a main-file raw comment, tagged with its source
// offset so the two streams can be merged into a single, source-ordered
// sequence. This is deliberately not ir::Document — §3.2's decl/comment
// interleave is the input to the structure pass, not the structure
// itself.
struct SourceItem {
    enum class Kind { Declaration, Comment };

    Kind     kind   = Kind::Declaration;
    unsigned offset = 0;
    // For a Declaration, "<DeclKindName> <name>" (design §3.1's decl label);
    // for a Comment, the raw comment text verbatim (design §3.4 strips
    // docblocks from the synopsis later — here nothing is filtered yet).
    std::string label;
};

// The result of collect_interleaved. `items` is the interleaved
// sequence; `had_parse_error` reports whether Clang's own
// DiagnosticsEngine flagged an error somewhere in the parse that produced
// `items` (see parse_header's ParsedHeader, frontend.cpp, for why a non-null
// AST does not already imply this). `items` can be non-empty *and* wrong at
// the same time when this is set — an unresolved
// out-of-line definition survives extraction lexically and reads as an
// entirely different, unlinked function — so a caller that cares about
// correctness, not just about seeing something, has to check this field
// rather than only `items.empty()`.
struct InterleaveResult {
    std::vector<SourceItem> items;
    bool                    had_parse_error = false;
};

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
// manipulation with no Clang dependency of its own, which is why it is
// declared here rather than kept file-local: it is unit-testable without
// building an AST.
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
// `args` is what `parse_header` (frontend.cpp) appends between its own fixed
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

// Parse the header at `header_path` with the tool setup from design §3.1
// (`-std=c++2c -fsyntax-only -fparse-all-comments`, with `options`'s resolved
// arguments spliced in between the two per the precedence and ordering
// documented on ParseOptions and resolve_extra_args), collect its main-file
// top-level declarations and main-file raw comments, and interleave both by
// source offset (design §3.2). Returns an empty `items` (and `had_parse_error`
// false) if the file cannot be read or Clang fails to build any AST from it
// at all; a non-empty `items` is not itself a guarantee the parse was clean —
// see InterleaveResult. dump-decls (tools/specgen/main.cpp) is the one caller
// that keeps running on `had_parse_error` rather than failing on it: its
// whole point is showing exactly what the parse produced, partial or not,
// which build_document's caller (generate) must not do — see BuildFailure.
InterleaveResult collect_interleaved(std::string_view header_path, const ParseOptions& options = {});

// A parsed `\rSec<depth>[<stable>]{<title>}` structure marker (design §3.2).
struct SectionHeader {
    int         depth = 0;
    std::string stable;
    std::string title;
};

// Recognizes a `\rSec<depth>[<stable>]{<title>}` structure marker in `raw`,
// the raw comment text verbatim (decoration and all, as
// RawComment::getRawText() returns it -- see SourceItem::label). Tolerant of
// "//", "///", or "//!" decoration and horizontal whitespace (never a
// newline) around the tag and its brackets; the bracket/brace contents
// themselves are taken verbatim. Built on the combinator library
// (decision parser-combinators, foundation/parse/{cursor,parser}.hpp):
//
//  - Failing before any input is consumed means "not a \rSec comment at
//    all" -- a `\ref{...}` synopsis-group header, license/SPDX text, a
//    trailing-brace comment. These are not errors.
//  - Failing after the tag is recognized -- a missing/malformed bracket, a
//    non-numeric or out-of-range depth, a missing stable name -- reports a
//    positioned parse_error instead. An out-of-range depth is one of these
//    positioned failures like any other, never a crash.
//
// build_document()'s classify() step (decision document-build-stages) turns
// every failure into a document_build::Ignored event either way -- the
// comment is not structure, so no node is added in either case -- but it
// does not treat them alike: an
// ordinary non-match stays a bare Ignored{}, while a malformed marker's
// Ignored carries a document_build::Diagnostic built from the position above,
// which build_document()'s caller can surface instead of silently dropping.
// A numbered draft-style heading ending in a dotted `[stable.name]` gets the
// same channel: it is not parsed as structure, but a Warning reports it
// rather than silently leaving the preceding section open. This is why
// parse_rsec is declared here rather than kept file-local to
// frontend.cpp: it makes the positioned failures directly testable.
beman::specgen::foundation::parse::parse_result<SectionHeader> parse_rsec(std::string_view raw);

// Build the document-tree skeleton (design §3.2) for the header at
// `header_path`: walk the same offset-ordered decl/comment stream as
// collect_interleaved(), but fold it into an ir::Document instead of a flat
// list.
//
// A three-stage pipeline (decision document-build-stages): classify() converts each raw
// decl/comment into a beman::specgen::document_build::DocEvent (the only
// stage that touches a clang::Decl*), document_build::build_tree() folds the
// `\rSec<n>[stable]{Title}` markers into nested ir::Section frames (closing
// any open frame at depth >= n first, every other decl becoming a child of
// whichever frame is open), and document_build::group_items() joins
// `\also`/empty-descr followers onto their primary as a post-pass. The latter
// two are clang-free and unit-tested with synthetic events/trees in
// tests/beman/specgen/document_build.test.cpp.
//
// A class/struct/union (or class template) decl yields an ir::Synopsis; an
// out-of-line function definition yields an ir::SpecItem (itemdecl + lowered
// itemdescr + derived Constraints/Mandates/equiv). A defined class or class
// template gets its synopsis text subtractively extracted and reformatted
// with the draft FormatStyle (design §3.4, §3.6); a forward declaration keeps
// an empty Synopsis. In-class-defined members and hidden friends carrying a
// docblock are collected from the class body and attached into the section
// named by the `\ref{stable}` group they sit under, ordered against
// out-of-line siblings by class-body position (design §3.3). Other
// comments (license/SPDX text, trailing-brace comments) are structural noise
// and dropped.
//
// build_document()'s hard-failure modes (decision expected-error-taxonomy: a
// single fallible step -> std::expected + and_then), two of them,
// deliberately worded apart. The first is the case collect_interleaved()
// silently empties on instead: the header cannot be read, or Clang cannot
// build any AST from it at all. The second is different in
// kind, not degree: Clang *did* build an AST, but its own DiagnosticsEngine
// reports an error somewhere in the parse that produced it
// (parse_header's ParsedHeader::had_error, frontend.cpp) — a header
// `generate` must not turn into wording, because extraction is lexical and a
// partial parse's visible symptom is not a missing type but a
// plausible-looking wrong one (an unresolved out-of-line definition reads as
// an unlinked, differently-qualified function). This is unrelated to, and
// does not touch, the docblock findings
// `generate` prints alongside a *successful* build (it still exits 0 on
// those): a docblock Error is markup the tool read correctly and disagreed
// with, while this failure means the tool did not read the code at all.
// `message` is ready to print as-is either way (main() does not need to
// reconstruct it).
struct BuildFailure {
    std::string message;
};

// On success: the document, plus every Diagnostic classify() and build_tree()
// collected along the way — docblock findings, malformed markers, boundary
// warnings. Most inputs produce none; the document is the same either way,
// so a caller that does not care about diagnostics can ignore the field and
// read the document alone.
//
// `options` reaches through to the same `parse_header` call
// collect_interleaved uses; see ParseOptions for the three sources and their
// precedence.
std::expected<beman::specgen::document_build::BuildResult, BuildFailure>
build_document(std::string_view header_path, const ParseOptions& options = {});

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_FRONTEND_HPP
