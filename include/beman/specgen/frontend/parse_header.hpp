// include/beman/specgen/frontend/parse_header.hpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The front end's one parse path (design §3.1): a header, the fixed
// arguments every entry point uses, and the resolved extra arguments, into a
// Clang AST, together with the files that parse read.

#ifndef BEMAN_SPECGEN_FRONTEND_PARSE_HEADER_HPP
#define BEMAN_SPECGEN_FRONTEND_PARSE_HEADER_HPP

#include <beman/specgen/frontend/build_failure.hpp>
#include <beman/specgen/frontend/compile_args.hpp>

#include <clang/Frontend/ASTUnit.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace beman::specgen::frontend {

// Result of parse_header: `ast` is null exactly when the header could
// not be read or Clang could not build any AST from it at all;
// `had_error` reports whether
// Clang's own DiagnosticsEngine flagged an error somewhere during the parse
// that *did* produce `ast`. The two are independent — buildASTFromCodeWithArgs
// recovers as best it can after a fatal preprocessor error (an unsatisfiable
// `#include`, the corpus case tests/corpus/include_path exercises) and still
// hands back a non-null ASTUnit, so `ast != nullptr` alone cannot tell a clean
// parse from a partial one. hasErrorOccurred(), not
// hasUncompilableErrorOccurred(): the latter is about a later codegen stage
// this `-fsyntax-only` tier never reaches, so it is the wrong question for a
// syntax-only parse to ask.
struct ParsedHeader {
    std::unique_ptr<clang::ASTUnit> ast;
    bool                            had_error = false;
};

// Shared parse path (design §3.1): slurp the header as text — buildASTFromCodeWithArgs
// takes the source by value and a separate display path, rather than opening the file
// itself (a real compilation database's ClangTool would; this tier does not stand one
// up) — then build the AST with the fixed args every front-end entry point
// uses, plus whatever `options` resolves to. `ast` is null on either
// failure so callers share one "give up" check; see ParsedHeader for why a
// non-null `ast` is not by itself "the parse went fine".
ParsedHeader parse_header(std::string_view header_path, const ParseOptions& options);

// Every file the parse read, for build_document_with_sources (frontend.hpp):
// `header_path` first, then the non-system files the preprocessor opened,
// sorted.
//
// The main file is taken from `header_path` rather than found in the walk,
// because it is not there to find: parse_header hands Clang the source as an
// in-memory buffer with a display path, so no FileEntry is ever created for it
// and nothing in the SourceManager's FileInfos names it. A dependency list
// missing the one file the caller named would be worse than no list at all.
//
// System headers are dropped -- `-MMD` semantics rather than `-MD`. Wording
// really can move when the standard library under it moves, but a `.d` naming
// every libstdc++ header buries the three that a reader is scanning for, and
// the paths it would name are a property of the box that ran the tool rather
// than of the paper.
std::vector<std::string> parse_sources(std::string_view header_path, const clang::ASTUnit& ast);

// The one failure collect_interleaved and build_document share, worded once so
// `dump-decls` and `generate` say the same thing about the same missing file.
BuildFailure unparsed_header(std::string_view header_path);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_PARSE_HEADER_HPP
