// src/beman/specgen/frontend/decl_projections.cpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/decl_projections.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <llvm/Support/Casting.h>

namespace beman::specgen::frontend {

const clang::FunctionDecl* as_out_of_line_member(const clang::Decl* decl) {
    const auto* fn = llvm::dyn_cast<clang::FunctionDecl>(decl);
    if (fn == nullptr)
        if (const auto* tmpl = llvm::dyn_cast<clang::FunctionTemplateDecl>(decl))
            fn = tmpl->getTemplatedDecl();
    if (fn == nullptr || !fn->isOutOfLine())
        return nullptr;
    const clang::FunctionDecl* first = fn->getFirstDecl();
    return first != fn && llvm::isa<clang::CXXRecordDecl>(first->getLexicalDeclContext()) ? fn : nullptr;
}

const clang::FunctionDecl* function_or_template(const clang::Decl* decl) {
    if (const auto* fn = llvm::dyn_cast<clang::FunctionDecl>(decl))
        return fn;
    if (const auto* tmpl = llvm::dyn_cast<clang::FunctionTemplateDecl>(decl))
        return tmpl->getTemplatedDecl();
    return nullptr;
}

bool is_first_declaration(const clang::Decl* decl) {
    if (const auto* fn = llvm::dyn_cast<clang::FunctionDecl>(decl))
        return fn->getFirstDecl() == fn;
    if (const auto* tmpl = llvm::dyn_cast<clang::FunctionTemplateDecl>(decl))
        return tmpl->getFirstDecl() == tmpl;
    if (const auto* var = llvm::dyn_cast<clang::VarDecl>(decl))
        return var->getFirstDecl() == var;
    return true;
}

} // namespace beman::specgen::frontend
