// src/beman/specgen/frontend/mandates.cpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/mandates.hpp>

#include <beman/specgen/conjuncts.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/frontend/constraints.hpp>
#include <beman/specgen/frontend/requires_clauses.hpp>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/TypeBase.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Token.h>
#include <llvm/Support/Casting.h>

#include <cstddef>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <vector>

namespace beman::specgen::frontend {

namespace {

const clang::Decl* single_decl(const clang::Stmt* stmt) {
    const auto* decl_stmt = llvm::dyn_cast<clang::DeclStmt>(stmt);
    return decl_stmt != nullptr && decl_stmt->isSingleDecl() ? decl_stmt->getSingleDecl() : nullptr;
}

} // namespace

MandatesPrologue mandates_prologue(const clang::CompoundStmt* body) {
    const std::vector<const clang::Stmt*> stmts(body->body_begin(), body->body_end());
    std::size_t                           pos = 0;
    // substrate generic algorithm: the shared cursor partitions one source
    // sequence into aliases, assertions, and the unconsumed remainder.
    while (pos < stmts.size() && llvm::isa_and_nonnull<clang::TypeAliasDecl>(single_decl(stmts[pos])))
        ++pos;
    const std::size_t alias_count = pos;

    MandatesPrologue result;
    // substrate generic algorithm: continue the same partitioning cursor while
    // recording the middle run for both derivation and source deletion.
    while (pos < stmts.size() && llvm::isa_and_nonnull<clang::StaticAssertDecl>(single_decl(stmts[pos]))) {
        result.assertions.push_back(llvm::cast<clang::DeclStmt>(stmts[pos]));
        ++pos;
    }

    if (result.assertions.empty())
        result.extraction_first = stmts.empty() ? nullptr : stmts.front();
    else if (alias_count != 0)
        result.extraction_first = stmts.front();
    else if (pos < stmts.size())
        result.extraction_first = stmts[pos];
    return result;
}

std::optional<beman::specgen::ir::DescriptionElement>
derive_mandates(const clang::FunctionDecl*                       fn,
                const clang::SourceManager&                      sm,
                const clang::LangOptions&                        lang_opts,
                const std::set<std::string>&                     ns_drop_set,
                const std::map<const clang::Decl*, std::string>& expos_set) {
    namespace ir = beman::specgen::ir;

    const auto* body = llvm::dyn_cast_or_null<clang::CompoundStmt>(fn->getBody());
    if (body == nullptr)
        return std::nullopt;

    // The conjuncts of one static_assert's condition, flattened at top-level
    // `&&` (split_conjuncts).
    const auto conjuncts_of = [](const clang::DeclStmt* stmt) {
        std::vector<const clang::Expr*> out;
        const auto*                     assert_decl = llvm::cast<clang::StaticAssertDecl>(stmt->getSingleDecl());
        split_conjuncts(assert_decl->getAssertExpr(), out);
        return out;
    };

    const MandatesPrologue                prologue = mandates_prologue(body);
    const std::vector<const clang::Expr*> leaves   = prologue.assertions | std::views::transform(conjuncts_of) |
                                                     std::views::join |
                                                     std::ranges::to<std::vector<const clang::Expr*>>();
    if (leaves.empty())
        return std::nullopt;

    const std::vector<ir::Paragraph> paragraphs =
        leaves | std::views::transform([&](const clang::Expr* leaf) {
            return phrase_conjunct(leaf, sm, lang_opts, ns_drop_set, expos_set);
        }) |
        std::ranges::to<std::vector<ir::Paragraph>>();

    ir::DescriptionElement element;
    element.kind      = ir::ElementKind::Mandates;
    element.derived   = true;
    element.conjuncts = paragraphs;
    beman::specgen::conjuncts::render_into(paragraphs, element);
    return element;
}

} // namespace beman::specgen::frontend
