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
// These entry points are the ClangTool decl-collection and extraction path.
// The link probe is frontend/smoke_check.hpp, and the crash handler is
// frontend/crash_handler.hpp.

#include <beman/specgen/document_build.hpp>
#include <beman/specgen/frontend/compile_args.hpp>
#include <beman/specgen/ir.hpp>

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace beman::specgen::frontend {

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

// Build the document-tree skeleton (design §3.2) for the header at
// `header_path`: walk the same offset-ordered decl/comment stream as
// collect_interleaved(), but fold it into an ir::Document instead of a flat
// list.
//
// A three-stage pipeline (decision document-build-stages): classify() converts each raw
// decl/comment into a beman::specgen::document_build::DocEvent (the only
// stage that touches a clang::Decl*), document_build::build_tree() folds the
// `\rSec<n>[stable]{Title}` markers into nested ir::Section frames (closing
// any open frame at depth >= n first, and closing the current frame at an
// exact matching Doxygen `/// END [stable]` fence; every other decl becomes a
// child of whichever frame is open), and document_build::group_items() joins
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
// deliberately worded apart. The first is the one collect_interleaved()
// shares: the header cannot be read, or Clang cannot build any AST from it
// at all. The second is different in
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

// Parse the header at `header_path` with the tool setup from design §3.1
// (`-std=c++2c -fsyntax-only -fparse-all-comments`, with `options`'s resolved
// arguments spliced in between the two per the precedence and ordering
// documented on ParseOptions and resolve_extra_args), collect its main-file
// top-level declarations and main-file raw comments, and interleave both by
// source offset (design §3.2). Fails with build_document's own BuildFailure
// if the file cannot be read or Clang fails to build any AST from it at all:
// an empty header is a valid, empty interleave, so an empty `items` cannot
// also mean "there was no header" (issue #144). A successful result is not
// itself a guarantee the parse was clean — see InterleaveResult. dump-decls
// (tools/specgen/main.cpp) is the one caller that keeps running on
// `had_parse_error` rather than failing on it: its whole point is showing
// exactly what the parse produced, partial or not, which build_document's
// caller (generate) must not do — see BuildFailure.
std::expected<InterleaveResult, BuildFailure> collect_interleaved(std::string_view    header_path,
                                                                  const ParseOptions& options = {});

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

// What build_document builds, plus the files it had to read to build it: the
// header itself first, then every non-system file the preprocessor opened for
// it, sorted. This is what a Makefile dependency fragment's prerequisite list
// is made of (depfile.hpp), and the front end is the only tier that can answer
// it -- a document is not a record of where its declarations came from, and by
// the time the driver holds one the parse is over.
//
// It over-approximates the set a change to the *wording* could come from, and
// deliberately, exactly as a compiler's `-MD` does: extraction is lexical, but
// a typedef or a default argument in an implementation header the document
// never renders can still change what the derived wording says, and a
// dependency edge too many costs a spurious rebuild while one too few costs a
// stale paper.
//
// Why this and not `build_document` with a wider return: `build_document` has
// sixty-odd call sites, almost all of them tests that want the document and
// nothing else, and document_build::BuildResult is build_tree()'s return --
// Tier A, clang-free, and in no position to know what a file is. So the richer
// entry point is the one with the longer name, and the older one is a
// projection of it rather than a second implementation.
struct DocumentBuild {
    beman::specgen::document_build::BuildResult result;
    std::vector<std::string>                    sources;
};

// A source file whose out-of-line member definitions carry the markup for
// members the header declares (issue #149), and the options it is parsed with:
// its own compile command's, since it is a translation unit of its own. Named
// with `generate --source`, after the header whose document it belongs to.
struct SourceFile {
    std::string  path;
    ParseOptions options;
};

// `source_files` are parsed after the header, each on its own, and a
// documented member definition in one becomes the item of the declaration it
// defines, placed as a definition in the header would be. Their reads join
// `sources`.
std::expected<DocumentBuild, BuildFailure> build_document_with_sources(
    std::string_view header_path, const ParseOptions& options = {}, const std::vector<SourceFile>& source_files = {});

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_FRONTEND_HPP
