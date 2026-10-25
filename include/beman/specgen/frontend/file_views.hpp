// include/beman/specgen/frontend/file_views.hpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Per-file views of a document that can span several files (issue #77):
// the text and comments of the file a location is in, how a diagnostic names
// that file, and the preprocessor ranges skipped in each.

#ifndef BEMAN_SPECGEN_FRONTEND_FILE_VIEWS_HPP
#define BEMAN_SPECGEN_FRONTEND_FILE_VIEWS_HPP

#include <clang/AST/ASTContext.h>
#include <clang/AST/RawCommentList.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/ASTUnit.h>
#include <llvm/ADT/StringRef.h>

#include <map>
#include <string>
#include <vector>

namespace beman::specgen::frontend {

// The buffer and the comment map of the file a location is in, rather than of
// the main file (issue #77).  Extraction is per declaration and every offset it
// computes is local to that declaration's own file, so a document that spans
// several files reads each one's text where it used to read the main file's.
// For a document that is one file these are the main file, as before.
llvm::StringRef file_buffer(const clang::SourceManager& sm, clang::SourceLocation loc);

const std::map<unsigned, clang::RawComment*>*
file_comments(const clang::ASTContext& ctx, const clang::SourceManager& sm, clang::SourceLocation loc);

// How a diagnostic names the file a finding is in: empty for the main file,
// which the driver already names, and otherwise the path relative to the main
// file's own directory when the file is under it -- `document/errors.hpp` --
// so a finding reads the way the `#include` that pulled the file in does.
std::string diagnostic_file_name(const clang::SourceManager& sm, clang::FileID file);

// A skipped preprocessor range, and the file it is in: the ranges are compared
// against offsets local to one declaration's own file, and a document can span
// several (issue #77), so a range from another file must not match.
struct SkippedRange {
    clang::FileID file;
    unsigned      begin = 0;
    unsigned      end   = 0;
};

using SkippedRanges = std::vector<SkippedRange>;

SkippedRanges collect_skipped_ranges(clang::ASTUnit& ast);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_FILE_VIEWS_HPP
