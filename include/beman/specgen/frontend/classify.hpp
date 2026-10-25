// include/beman/specgen/frontend/classify.hpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Stage 1 of the document build (decision document-build-stages): each raw
// declaration or comment, in document order, classified into a
// document_build::DocEvent. The only stage that touches a clang::Decl*.

#ifndef BEMAN_SPECGEN_FRONTEND_CLASSIFY_HPP
#define BEMAN_SPECGEN_FRONTEND_CLASSIFY_HPP

#include <beman/specgen/document_build.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/frontend/attach.hpp>
#include <beman/specgen/frontend/build_failure.hpp>
#include <beman/specgen/frontend/compile_args.hpp>
#include <beman/specgen/frontend/declaration_masks.hpp>
#include <beman/specgen/frontend/document_files.hpp>
#include <beman/specgen/frontend/file_views.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/ASTUnit.h>

#include <expected>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

// One offset-ordered raw item: either a still-typed top-level decl or a
// comment's raw text, collected before classify() (the pipeline's first
// stage) converts it to a document_build::DocEvent. Mirrors SourceItem,
// but keeps the clang::Decl* (SourceItem only keeps a formatted label) so
// classify() can dispatch on it. Not to be confused with
// document_build::DocEvent, classify's clang-free output.
struct RawItem {
    unsigned     offset = 0;
    clang::Decl* decl   = nullptr; // non-null: a decl event
    std::string  comment_text;     // decl == nullptr: a comment event
};

// The main-file byte ranges of raw comments some processed declaration
// consumed as its own docblock (top-level decls and class members alike).
// classify() consults this for a comment carrying a verbatim marker: attached,
// the block belongs to its declaration's event, and the comment's own RawItem
// must not become a second node (issue #4).
using AttachedCommentRanges = std::vector<std::pair<unsigned, unsigned>>;

// The function `decl` declares at namespace scope, when it is a declaration
// without a body: the shape whose definition may be in a source file (issue
// #175). A member, a hidden friend and a definition are none of them it.
const clang::FunctionDecl* declared_free_function(const clang::Decl* decl);

beman::specgen::document_build::DocEvent classify(const RawItem&                                   ev,
                                                  const clang::SourceManager&                      sm,
                                                  const DocumentFiles&                             doc,
                                                  const clang::LangOptions&                        lang_opts,
                                                  const std::set<const clang::Decl*>&              omit_set,
                                                  const std::map<const clang::Decl*, std::string>& expos_set,
                                                  const SeeBelowMap&                               seebelow_map,
                                                  const FreestandingMap&                           freestanding_map,
                                                  const std::set<std::string>&                     ns_drop_set,
                                                  const SkippedRanges&                             skipped,
                                                  const AttachedCommentRanges&                     attached_comments,
                                                  const SourceMarkupMap&                           source_markup);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_CLASSIFY_HPP
