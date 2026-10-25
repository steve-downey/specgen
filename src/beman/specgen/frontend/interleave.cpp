// src/beman/specgen/frontend/interleave.cpp                      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/interleave.hpp>

#include <beman/specgen/frontend/build_failure.hpp>
#include <beman/specgen/frontend/compile_args.hpp>
#include <beman/specgen/frontend/document_files.hpp>
#include <beman/specgen/frontend/parse_header.hpp>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/RawCommentList.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <llvm/Support/Casting.h>

#include <algorithm>
#include <expected>
#include <map>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace beman::specgen::frontend {

namespace {

std::string decl_label(clang::Decl* decl) {
    std::string label = decl->getDeclKindName();
    if (const auto* named = llvm::dyn_cast<clang::NamedDecl>(decl)) {
        label += ' ';
        label += named->getNameAsString();
    }
    return label;
}

} // namespace

std::expected<InterleaveResult, BuildFailure> collect_interleaved(std::string_view    header_path,
                                                                  const ParseOptions& options) {
    InterleaveResult result;

    ParsedHeader parsed = parse_header(header_path, options);
    if (!parsed.ast)
        return std::unexpected(unparsed_header(header_path));
    // Carried through rather than acted on here — dump-decls, this
    // function's one caller, is the debugging view of exactly what the parse
    // produced, so seeing a partial collection when the parse is partial is
    // the entire point of the command (see InterleaveResult, BuildFailure).
    result.had_parse_error = parsed.had_error;

    clang::ASTContext&    ctx = parsed.ast->getASTContext();
    clang::SourceManager& sm  = parsed.ast->getSourceManager();
    // The same document rule build_document uses, so the debugging view shows
    // what the wording is generated from and not a narrower file (issue #77).
    const DocumentFiles doc = DocumentFiles::discover(*parsed.ast, sm);

    // Main-file top-level declarations (design §3.1: "process only decls whose
    // location is in the main file"). top_level_begin()/end() is a
    // std::vector<Decl*>::iterator pair with no range object of its own;
    // std::ranges::subrange gives it one.
    std::vector<clang::Decl*> decls;
    // substrate generic algorithm: a for_each-shaped walk driving
    // collect_top_level_decl's side effect (recursively appending to
    // `decls`), not a transform building a fresh container -- this tree
    // declines ranges::for_each as a costume for a loop with a side effect
    // (see collect_inclass_items's docblock-comment note above).
    for (clang::Decl* decl : std::ranges::subrange(parsed.ast->top_level_begin(), parsed.ast->top_level_end()))
        collect_top_level_decl(decl, sm, doc, decls);
    result.items.append_range(
        decls | std::views::transform([&](clang::Decl* decl) {
            return SourceItem{SourceItem::Kind::Declaration, doc.offset(sm, decl->getBeginLoc()), decl_label(decl)};
        }));

    // The document's raw comments (design §3.1/§3.2), keyed by begin offset;
    // getCommentsInFile may return null when a file has none.
    const auto collect_comments = [&](clang::FileID file) {
        if (const std::map<unsigned, clang::RawComment*>* comments = ctx.Comments.getCommentsInFile(file)) {
            result.items.append_range(*comments | std::views::transform([&](const auto& kv) {
                return SourceItem{
                    SourceItem::Kind::Comment, doc.offset_in(file, kv.first), kv.second->getRawText(sm).str()};
            }));
        }
    };
    collect_comments(doc.main());
    // substrate generic algorithm: a for_each-shaped walk driving
    // collect_comments' side effect over the followed files.
    for (const DocumentFiles::Part& part : doc.parts())
        collect_comments(part.file);

    // Design §3.2: "collect top-level decls and raw comments; sort by source
    // offset; interleave." Ties keep decls before comments, which cannot
    // happen in practice — a comment and a decl cannot begin at the same byte.
    std::stable_sort(result.items.begin(), result.items.end(), [](const SourceItem& a, const SourceItem& b) {
        return a.offset < b.offset;
    });

    return result;
}

} // namespace beman::specgen::frontend
