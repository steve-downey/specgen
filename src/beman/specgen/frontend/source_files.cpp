// src/beman/specgen/frontend/source_files.cpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/source_files.hpp>

#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/document_build.hpp>
#include <beman/specgen/frontend/attach.hpp>
#include <beman/specgen/frontend/body_uses.hpp>
#include <beman/specgen/frontend/build_failure.hpp>
#include <beman/specgen/frontend/decl_projections.hpp>
#include <beman/specgen/frontend/directive_sets.hpp>
#include <beman/specgen/frontend/docblock_lookup.hpp>
#include <beman/specgen/frontend/document_files.hpp>
#include <beman/specgen/frontend/file_views.hpp>
#include <beman/specgen/frontend/parse_header.hpp>
#include <beman/specgen/frontend/source_file.hpp>
#include <beman/specgen/frontend/record_members.hpp>
#include <beman/specgen/frontend/classify.hpp>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclFriend.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RawCommentList.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/FileEntry.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Token.h>
#include <llvm/Support/Casting.h>

#include <algorithm>
#include <array>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

namespace {

DeclPosition decl_position(const clang::Decl* decl, const clang::SourceManager& sm, std::string_view main_path) {
    const auto [file, offset] = sm.getDecomposedLoc(decl->getBeginLoc());
    return {canonical_file_path(sm, file, main_path), offset};
}

// The FriendDecl that declares `fn` in its class, whose `friend` keyword the
// itemdecl keeps, as collect_inclass_items keeps it for a hidden friend.
clang::SourceLocation friend_keyword(const clang::FunctionDecl* fn) {
    const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(fn->getLexicalDeclContext());
    if (record == nullptr || fn->getFriendObjectKind() == clang::Decl::FOK_None)
        return {};
    const clang::Decl* declared = fn->getDescribedFunctionTemplate() != nullptr
                                      ? static_cast<const clang::Decl*>(fn->getDescribedFunctionTemplate())
                                      : fn;
    const auto         friends  = record->friends();
    const auto         found    = std::ranges::find_if(friends, [declared](const clang::FriendDecl* friend_decl) {
        return friend_decl->getFriendDecl() == declared;
    });
    return found != friends.end() ? (*found)->getBeginLoc() : clang::SourceLocation{};
}

// The declarations a parse holds in the files `wanted` accepts, descending
// through namespaces and linkage specifications.
template <class Wanted>
void collect_file_decls(clang::Decl*                decl,
                        const clang::SourceManager& sm,
                        const Wanted&               wanted,
                        std::vector<clang::Decl*>&  out) {
    if (llvm::isa<clang::NamespaceDecl, clang::LinkageSpecDecl>(decl)) {
        // substrate generic algorithm: recursive descent over a decl tree, the
        // same shape as collect_top_level_decl's.
        for (clang::Decl* child : llvm::cast<clang::DeclContext>(decl)->decls())
            collect_file_decls(child, sm, wanted, out);
        return;
    }
    if (decl->isImplicit() || !decl->getBeginLoc().isValid())
        return;
    if (wanted(sm.getFileID(sm.getExpansionLoc(decl->getBeginLoc()))))
        out.push_back(decl);
}

} // namespace

std::string canonical_file_path(const clang::SourceManager& sm, clang::FileID file, std::string_view main_path) {
    std::string path;
    if (file == sm.getMainFileID())
        path = std::string(main_path);
    else if (const clang::OptionalFileEntryRef entry = sm.getFileEntryRefForID(file))
        path = entry->getName().str();
    else
        return {};
    std::error_code ec;
    return std::filesystem::weakly_canonical(path, ec).generic_string();
}

std::expected<SourceHarvest, BuildFailure> harvest_source_file(const SourceFile&            source,
                                                               const std::set<std::string>& document_paths) {
    namespace db = beman::specgen::document_build;

    ParsedHeader parsed = parse_header(source.path, source.options);
    if (!parsed.ast)
        return std::unexpected(unparsed_header(source.path));
    if (parsed.had_error)
        return std::unexpected(BuildFailure{std::format(
            "Clang reported an error parsing '{}'; the parse is incomplete, so the wording generated from it "
            "would be unreliable",
            source.path)});

    const clang::SourceManager& sm        = parsed.ast->getSourceManager();
    const clang::LangOptions&   lang_opts = parsed.ast->getLangOpts();
    const clang::FileID         main_file = sm.getMainFileID();

    std::map<clang::FileID, bool> in_document;
    const auto                    is_document_file = [&](clang::FileID file) {
        const auto [it, inserted] = in_document.try_emplace(file, false);
        if (inserted)
            it->second = document_paths.contains(canonical_file_path(sm, file, source.path));
        return it->second;
    };

    // The header's declarations as this parse sees them, for the same
    // pre-passes the header's parse makes, so an item built here spells its
    // itemdecl and rewrites its body as it would there.
    std::vector<clang::Decl*> header_decls;
    std::vector<clang::Decl*> source_decls;
    std::vector<clang::Decl*> expos_decls;
    // substrate generic algorithm: the same iterator-pair-turned-subrange,
    // for_each-shaped side effect as build_document_with_sources' walks.
    for (clang::Decl* decl : std::ranges::subrange(parsed.ast->top_level_begin(), parsed.ast->top_level_end())) {
        collect_file_decls(decl, sm, is_document_file, header_decls);
        collect_file_decls(decl, sm, [main_file](clang::FileID file) { return file == main_file; }, source_decls);
        collect_expos_scope_decl(decl, sm, main_file, expos_decls);
    }
    expos_decls.append_range(header_decls);

    const std::map<const clang::Decl*, std::string> expos_set   = build_expos_set(expos_decls, sm);
    const std::set<std::string>                     ns_drop_set = build_namespace_drop_set(header_decls);
    const SkippedRanges                             skipped     = collect_skipped_ranges(*parsed.ast);

    SourceHarvest harvest;
    harvest.sources    = parse_sources(source.path, *parsed.ast);
    const auto line_of = [&](const clang::Decl* decl) { return sm.getSpellingLineNumber(decl->getBeginLoc()); };
    const auto report  = [&](const clang::Decl* decl, std::string message) {
        harvest.diagnostics.push_back(
            db::Diagnostic{beman::specgen::Severity::Error, line_of(decl), std::move(message), source.path});
    };

    // substrate generic algorithm: a filter-map with three distinct
    // rejections, each a finding, over the source file's declarations.
    for (const clang::Decl* decl : source_decls) {
        if (!has_docblock(decl, sm))
            continue;
        const clang::FunctionDecl* fn    = as_out_of_line_function(decl);
        const std::string          named = llvm::isa<clang::NamedDecl>(decl)
                                               ? llvm::cast<clang::NamedDecl>(decl)->getQualifiedNameAsString()
                                               : std::string{"this declaration"};
        if (fn == nullptr || !fn->isThisDeclarationADefinition()) {
            report(decl,
                   "a documented declaration in a source file produces no wording: a source file supplies markup "
                   "only on the definition of a function declared in the header");
            continue;
        }
        const clang::FunctionDecl* first             = fn->getFirstDecl();
        const bool                 declared_in_class = llvm::isa<clang::CXXRecordDecl>(first->getLexicalDeclContext());
        const bool                 declared_free     = !declared_in_class && declared_free_function(first) != nullptr;
        if (first == fn || (!declared_in_class && !declared_free)) {
            report(decl,
                   std::format("the documented definition of `{}` produces no wording: a source file supplies "
                               "markup only for a function declared in the header, in a class body or at "
                               "namespace scope",
                               named));
            continue;
        }
        if (!is_document_file(sm.getFileID(first->getBeginLoc()))) {
            report(decl,
                   std::format("the documented definition of `{}` produces no wording: its class is declared in "
                               "'{}', which is not part of this document",
                               named,
                               canonical_file_path(sm, sm.getFileID(first->getBeginLoc()), source.path)));
            continue;
        }

        AttachedItem attached =
            attach_function(fn, first, friend_keyword(first), sm, lang_opts, ns_drop_set, expos_set, skipped);
        // A finding positioned in this parse's main file names no file, which
        // the driver reads as the header; name the source file instead.
        std::ranges::for_each(attached.diagnostics, [&](db::Diagnostic& diagnostic) {
            if (diagnostic.file.empty())
                diagnostic.file = source.path;
        });
        if (has_unextracted_body(fn, sm))
            harvest.unextracted_uses.append_range(body_member_uses(fn, [&](const clang::CXXRecordDecl* parent) {
                return is_document_file(sm.getFileID(parent->getLocation()));
            }));
        harvest.items.emplace(decl_position(first, sm, source.path),
                              SourceMarkup{std::move(attached), source.path, line_of(fn)});
    }
    return harvest;
}

SourceMarkupMap match_source_markup(const std::vector<clang::Decl*>&                         decls,
                                    const clang::SourceManager&                              sm,
                                    std::string_view                                         header_path,
                                    const std::map<DeclPosition, SourceMarkup>&              items,
                                    std::vector<beman::specgen::document_build::Diagnostic>& diagnostics) {
    SourceMarkupMap        matched;
    std::set<DeclPosition> used;
    const auto             match_members = [&](this const auto& self, const clang::CXXRecordDecl* record) -> void {
        // substrate generic algorithm: a recursive walk over class bodies that
        // fills two lookups at once.
        for (const clang::Decl* member : record->decls()) {
            if (const clang::CXXRecordDecl* nested = as_record_decl(member);
                nested != nullptr && nested->isThisDeclarationADefinition())
                self(nested);
            const clang::FunctionDecl* fn = member_function_or_template(member);
            if (fn == nullptr)
                continue;
            const DeclPosition position = decl_position(fn, sm, header_path);
            if (const auto found = items.find(position); found != items.end()) {
                matched.emplace(fn->getCanonicalDecl(), &found->second);
                used.insert(position);
            }
        }
    };
    std::ranges::for_each(decls | std::views::transform(as_record_decl) |
                              std::views::filter([](const clang::CXXRecordDecl* record) {
                                  return record != nullptr && record->isThisDeclarationADefinition();
                              }),
                          match_members);
    // A namespace-scope function declared in the document is matched by the
    // position of that declaration (issue #175).
    std::ranges::for_each(decls | std::views::transform(declared_free_function) |
                              std::views::filter([](const clang::FunctionDecl* fn) { return fn != nullptr; }),
                          [&](const clang::FunctionDecl* fn) {
                              const DeclPosition position = decl_position(fn, sm, header_path);
                              if (const auto found = items.find(position); found != items.end()) {
                                  matched.emplace(fn->getCanonicalDecl(), &found->second);
                                  used.insert(position);
                              }
                          });
    std::ranges::for_each(items | std::views::filter([&](const auto& item) { return !used.contains(item.first); }),
                          [&](const auto& item) {
                              diagnostics.push_back(beman::specgen::document_build::Diagnostic{
                                  beman::specgen::Severity::Error,
                                  item.second.line,
                                  "this documented definition produces no wording: it defines no member or function "
                                  "this document declares",
                                  item.second.file});
                          });
    return matched;
}

} // namespace beman::specgen::frontend
