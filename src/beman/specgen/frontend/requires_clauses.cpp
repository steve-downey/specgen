// src/beman/specgen/frontend/requires_clauses.cpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/requires_clauses.hpp>

#include <beman/specgen/synopsis_edits.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/OperationKinds.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>

#include <string_view>
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

std::vector<synopsis_edits::SynopsisEdit> requires_clause_removals(const clang::FunctionDecl*  fn,
                                                                   const clang::SourceManager& sm,
                                                                   const clang::LangOptions&   lang_opts) {
    const auto past = [&](clang::SourceLocation loc) {
        return sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(loc, 0, sm, lang_opts)).second;
    };
    std::vector<synopsis_edits::SynopsisEdit> out;
    if (const clang::Expr* head = head_requires_clause(fn)) {
        const unsigned begin = past(function_head_parameters(fn)->getRAngleLoc());
        const unsigned end   = past(head->getEndLoc());
        if (end > begin)
            out.push_back(synopsis_edits::SynopsisEdit{begin, end, " "});
    }
    const clang::Expr* trailing = fn->getTrailingRequiresClause().ConstraintExpr;
    if (trailing != nullptr && trailing->getBeginLoc().isFileID()) {
        const auto [file, expr_begin]                    = sm.getDecomposedLoc(sm.getFileLoc(trailing->getBeginLoc()));
        const llvm::StringRef                buffer      = sm.getBufferData(file);
        static constexpr std::string_view    kWhitespace = " \t\n\v\f\r";
        static constexpr llvm::StringLiteral kRequires{"requires"};
        const llvm::StringRef                before = buffer.substr(0, expr_begin).rtrim(kWhitespace);
        if (before.ends_with(kRequires)) {
            const llvm::StringRef declarator = before.drop_back(kRequires.size()).rtrim(kWhitespace);
            const unsigned        end        = past(trailing->getEndLoc());
            if (end > declarator.size())
                out.push_back(synopsis_edits::SynopsisEdit{static_cast<unsigned>(declarator.size()), end, ""});
        }
    }
    return out;
}

} // namespace beman::specgen::frontend
