// src/beman/specgen/frontend/file_views.cpp                      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/file_views.hpp>

#include <clang/AST/ASTContext.h>
#include <clang/AST/RawCommentList.h>
#include <clang/Basic/FileEntry.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/PreprocessingRecord.h>
#include <clang/Lex/Preprocessor.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Path.h>

#include <map>
#include <string>

namespace beman::specgen::frontend {

llvm::StringRef file_buffer(const clang::SourceManager& sm, clang::SourceLocation loc) {
    return sm.getBufferData(sm.getFileID(loc));
}

const std::map<unsigned, clang::RawComment*>*
file_comments(const clang::ASTContext& ctx, const clang::SourceManager& sm, clang::SourceLocation loc) {
    return ctx.Comments.getCommentsInFile(sm.getFileID(loc));
}

std::string diagnostic_file_name(const clang::SourceManager& sm, clang::FileID file) {
    if (file == sm.getMainFileID() || file.isInvalid())
        return {};
    const clang::OptionalFileEntryRef entry = sm.getFileEntryRefForID(file);
    const clang::OptionalFileEntryRef main  = sm.getFileEntryRefForID(sm.getMainFileID());
    if (!entry)
        return {};
    const llvm::StringRef name = entry->getName();
    if (main) {
        const llvm::StringRef dir = llvm::sys::path::parent_path(main->getName());
        if (!dir.empty() && name.starts_with(dir) && name.size() > dir.size() + 1)
            return name.substr(dir.size() + 1).str();
    }
    return llvm::sys::path::filename(name).str();
}

SkippedRanges collect_skipped_ranges(clang::ASTUnit& ast) {
    SkippedRanges               result;
    const clang::SourceManager& sm     = ast.getSourceManager();
    clang::PreprocessingRecord* record = ast.getPreprocessor().getPreprocessingRecord();
    if (record == nullptr)
        return result;

    // substrate generic algorithm: a filter-map whose bounds conversion needs
    // the SourceManager and can reject either endpoint after decomposition.
    for (const clang::SourceRange range : record->getSkippedRanges()) {
        if (!range.isValid())
            continue;
        const auto [begin_file, begin] = sm.getDecomposedLoc(range.getBegin());
        const clang::SourceLocation end_loc =
            clang::Lexer::getLocForEndOfToken(range.getEnd(), 0, sm, ast.getLangOpts());
        const auto [end_file, end] = sm.getDecomposedLoc(end_loc);
        if (begin_file == end_file && end > begin)
            result.emplace_back(begin_file, begin, end);
    }
    return result;
}

} // namespace beman::specgen::frontend
