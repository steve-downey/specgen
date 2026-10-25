// include/beman/specgen/frontend/interleave.hpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The decl/comment interleave (design §3.2) as a flat list: what the
// structure pass reads, before it becomes a document. `dump-decls` shows it.

#ifndef BEMAN_SPECGEN_FRONTEND_INTERLEAVE_HPP
#define BEMAN_SPECGEN_FRONTEND_INTERLEAVE_HPP

#include <beman/specgen/frontend/build_failure.hpp>
#include <beman/specgen/frontend/compile_args.hpp>

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
// `items` (see parse_header's ParsedHeader, parse_header.hpp, for why a non-null
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

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_INTERLEAVE_HPP
