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
#include <beman/specgen/frontend/build_failure.hpp>
#include <beman/specgen/frontend/compile_args.hpp>
#include <beman/specgen/ir.hpp>

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace beman::specgen::frontend {

// Build the document-tree skeleton (design §3.2) for the header at
// `header_path`: walk the same offset-ordered decl/comment stream as
// collect_interleaved() (interleave.hpp), but fold it into an ir::Document instead of a flat
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
