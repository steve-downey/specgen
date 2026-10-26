// include/beman/specgen/frontend/foreign_names.hpp               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The names the leakage check (design §9) compares: the paper's own
// entities, and the namespaces and declarations from outside it that a
// document names.

#ifndef BEMAN_SPECGEN_FRONTEND_FOREIGN_NAMES_HPP
#define BEMAN_SPECGEN_FRONTEND_FOREIGN_NAMES_HPP

#include <beman/specgen/document_build.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/frontend/declaration_masks.hpp>
#include <beman/specgen/frontend/document_files.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceManager.h>

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

std::optional<std::vector<std::string>> collect_paper_entities(const std::vector<clang::Decl*>& decls,
                                                               const clang::SourceManager&      sm);

std::vector<beman::specgen::ir::ForeignNamespace> collect_foreign_namespaces(const std::vector<clang::Decl*>& decls,
                                                                             const std::set<std::string>&     drop);

std::vector<beman::specgen::ir::ForeignDeclaration>
collect_foreign_declarations(const std::vector<clang::Decl*>&                 decls,
                             const std::map<const clang::Decl*, std::string>& expos,
                             const clang::SourceManager&                      sm,
                             const DocumentFiles&                             doc);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_FOREIGN_NAMES_HPP
