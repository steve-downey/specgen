// include/beman/specgen/frontend/source_files.hpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Source files (issue #149): the markup on out-of-line member definitions
// in a translation unit of their own, harvested and matched to the
// declarations the header makes.
//
// A member declared in the header and defined in a source file carries its
// markup on that definition, which the header's own parse never sees. Each
// source file named with --source is parsed on its own, with its own compile
// flags, and every documented out-of-line member definition in it is built
// into its item there, as attach_function builds one for a definition in the
// header. The source file includes the header, so the in-class declaration it
// defines is in that parse too, and the item is keyed by where that
// declaration is: its file and byte offset, which the header's parse reads
// identically. A definition that matches no declaration is a compile error in
// the source file, so a mistyped signature fails the parse rather than leaving
// a stray item.

#ifndef BEMAN_SPECGEN_FRONTEND_SOURCE_FILES_HPP
#define BEMAN_SPECGEN_FRONTEND_SOURCE_FILES_HPP

#include <beman/specgen/document_build.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/frontend/attach.hpp>
#include <beman/specgen/frontend/build_failure.hpp>
#include <beman/specgen/frontend/compile_args.hpp>
#include <beman/specgen/frontend/declaration_masks.hpp>
#include <beman/specgen/frontend/document_files.hpp>
#include <beman/specgen/frontend/file_views.hpp>
#include <beman/specgen/frontend/source_file.hpp>
#include <beman/specgen/frontend/classify.hpp>

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

// Where a declaration is, the same in any parse that reads its file: the
// file's canonical path, and the byte offset in it. `main_path` names the main
// file, which the parse holds as an in-memory buffer rather than a file it
// opened.
using DeclPosition = std::pair<std::string, unsigned>;

std::string canonical_file_path(const clang::SourceManager& sm, clang::FileID file, std::string_view main_path);

struct SourceHarvest {
    std::map<DeclPosition, SourceMarkup>                    items;
    std::vector<beman::specgen::document_build::Diagnostic> diagnostics;
    std::vector<std::string>                                sources;
    // The members named by the bodies of the definitions that became items
    // but render none of them (issue #176), as the header's own parse
    // collects them for a definition there.
    std::vector<std::pair<std::string, std::string>> unextracted_uses;
};

// Build the items for one source file's documented member definitions.
// `document_paths` are the canonical paths of the files the header's document
// is made of. Any other documented declaration in the source file produces no
// wording, and says so, rather than being dropped.
std::expected<SourceHarvest, BuildFailure> harvest_source_file(const SourceFile&            source,
                                                               const std::set<std::string>& document_paths);

// The source files' items, matched to the in-class declarations of the
// header's parse. Each class definition the document describes is walked,
// nested ones included, and an item that matches none of their members is
// reported: its class is in the document's files, but no class the document
// describes holds it.
SourceMarkupMap match_source_markup(const std::vector<clang::Decl*>&                         decls,
                                    const clang::SourceManager&                              sm,
                                    std::string_view                                         header_path,
                                    const std::map<DeclPosition, SourceMarkup>&              items,
                                    std::vector<beman::specgen::document_build::Diagnostic>& diagnostics);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_SOURCE_FILES_HPP
