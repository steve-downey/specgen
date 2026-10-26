// src/beman/specgen/frontend/requires_clauses.cpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/requires_clauses.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/OperationKinds.h>
#include <llvm/Support/Casting.h>

#include <vector>

namespace beman::specgen::frontend {

void split_conjuncts(const clang::Expr* e, std::vector<const clang::Expr*>& out) {
    if (const auto* bo = llvm::dyn_cast<clang::BinaryOperator>(e->IgnoreParens())) {
        if (bo->getOpcode() == clang::BO_LAnd) {
            split_conjuncts(bo->getLHS(), out);
            split_conjuncts(bo->getRHS(), out);
            return;
        }
    }
    out.push_back(e);
}

std::vector<const clang::Expr*> trailing_conjuncts(const clang::FunctionDecl* fn) {
    std::vector<const clang::Expr*> out;
    if (const clang::Expr* clause = fn->getTrailingRequiresClause().ConstraintExpr)
        split_conjuncts(clause, out);
    return out;
}

const clang::TemplateParameterList* head_parameters(const clang::Decl* head_decl) {
    const auto* tmpl = llvm::dyn_cast_or_null<clang::TemplateDecl>(head_decl);
    return tmpl != nullptr ? tmpl->getTemplateParameters() : nullptr;
}

const clang::TemplateParameterList* function_head_parameters(const clang::FunctionDecl* fn) {
    const clang::FunctionTemplateDecl* described = fn->getDescribedFunctionTemplate();
    return described != nullptr ? described->getTemplateParameters() : nullptr;
}

const clang::Expr* head_requires_clause(const clang::FunctionDecl* fn) {
    const clang::TemplateParameterList* params = function_head_parameters(fn);
    return params != nullptr ? params->getRequiresClause() : nullptr;
}

} // namespace beman::specgen::frontend
