// src/beman/specgen/frontend/merged_twins.cpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/merged_twins.hpp>

#include <beman/specgen/frontend/docblock_lookup.hpp>
#include <beman/specgen/frontend/requires_clauses.hpp>
#include <beman/specgen/synopsis_edits.hpp>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/OperationKinds.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/RawCommentList.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/TokenKinds.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Token.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <ranges>
#include <vector>

namespace beman::specgen::frontend {

using synopsis_edits::SynopsisEdit;

namespace {

bool is_merged(const clang::FunctionDecl* fn, const clang::SourceManager& sm) {
    if (docblock_directives(fn, sm).merge)
        return true;
    const clang::FunctionDecl* def = fn->getDefinition();
    return def != nullptr && def != fn && docblock_directives(def, sm).merge;
}

bool same_signature(const clang::FunctionDecl* a, const clang::FunctionDecl* b) {
    if (a->getDeclName() != b->getDeclName() || a->param_size() != b->param_size())
        return false;
    const clang::ASTContext& ctx = a->getASTContext();
    if (!std::ranges::equal(a->parameters(), b->parameters(), [&ctx](const auto* x, const auto* y) {
            return ctx.hasSameType(x->getType(), y->getType());
        }))
        return false;
    const auto* ma = llvm::dyn_cast<clang::CXXMethodDecl>(a);
    const auto* mb = llvm::dyn_cast<clang::CXXMethodDecl>(b);
    if ((ma == nullptr) != (mb == nullptr))
        return false;
    return ma == nullptr ||
           (ma->getMethodQualifiers() == mb->getMethodQualifiers() && ma->getRefQualifier() == mb->getRefQualifier());
}

// The trailing-clause conjuncts of every `\merge`d twin of `fn`, which may be
// any redeclaration of the survivor. Empty for a member template (a twin pair
// is a pair of special members, never templates) and for a twin itself.
std::vector<const clang::Expr*> merged_twin_conjuncts(const clang::FunctionDecl* fn, const clang::SourceManager& sm) {
    const clang::FunctionDecl* in_class = fn->getFirstDecl();
    const auto*                record   = llvm::dyn_cast<clang::CXXRecordDecl>(in_class->getDeclContext());
    if (record == nullptr || in_class->getDescribedFunctionTemplate() != nullptr || is_merged(in_class, sm))
        return {};
    return record->decls() | std::views::transform([](const clang::Decl* member) {
               return llvm::dyn_cast<clang::FunctionDecl>(member);
           }) |
           std::views::filter([&](const clang::FunctionDecl* twin) {
               return twin != nullptr && twin != in_class && same_signature(twin, in_class) && is_merged(twin, sm);
           }) |
           std::views::transform(trailing_conjuncts) | std::views::join | std::ranges::to<std::vector>();
}

bool complementary(const clang::Expr* a, const clang::Expr* b, const clang::ASTContext& ctx) {
    const auto negated = [](const clang::Expr* e) -> const clang::Expr* {
        const auto* op = llvm::dyn_cast<clang::UnaryOperator>(e->IgnoreParenImpCasts());
        return op != nullptr && op->getOpcode() == clang::UO_LNot ? op->getSubExpr()->IgnoreParenImpCasts() : nullptr;
    };
    const auto same = [&ctx](const clang::Expr* x, const clang::Expr* y) {
        llvm::FoldingSetNodeID x_id;
        llvm::FoldingSetNodeID y_id;
        x->Profile(x_id, ctx, /*Canonical=*/true);
        y->Profile(y_id, ctx, /*Canonical=*/true);
        return x_id == y_id;
    };
    const clang::Expr* not_a = negated(a);
    const clang::Expr* not_b = negated(b);
    return (not_a != nullptr && same(not_a, b->IgnoreParenImpCasts())) ||
           (not_b != nullptr && same(not_b, a->IgnoreParenImpCasts()));
}

} // namespace

std::vector<bool>
twin_cut_mask(const clang::FunctionDecl* fn, const clang::SourceManager& sm, const clang::LangOptions& lang_opts) {
    const std::vector<const clang::Expr*> twin = merged_twin_conjuncts(fn, sm);
    if (twin.empty())
        return {};
    const std::vector<const clang::Expr*> leaves = trailing_conjuncts(fn);
    if (std::ranges::any_of(leaves, [](const clang::Expr* leaf) {
            return leaf->getBeginLoc().isMacroID() || leaf->getEndLoc().isMacroID();
        }))
        return {};
    const auto flat_separator = [&](const clang::Expr* left, const clang::Expr* right) {
        const clang::CharSourceRange between = clang::CharSourceRange::getCharRange(
            clang::Lexer::getLocForEndOfToken(left->getEndLoc(), 0, sm, lang_opts), right->getBeginLoc());
        const llvm::StringRef text = clang::Lexer::getSourceText(between, sm, lang_opts).trim();
        return text == "&&" || text == "and";
    };
    if (!std::ranges::all_of(leaves | std::views::adjacent<2>,
                             [&](const auto& pair) { return flat_separator(std::get<0>(pair), std::get<1>(pair)); }))
        return {};
    const clang::ASTContext& ctx  = fn->getASTContext();
    std::vector<bool>        mask = leaves | std::views::transform([&](const clang::Expr* leaf) {
                                 return std::ranges::any_of(
                                     twin, [&](const clang::Expr* other) { return complementary(leaf, other, ctx); });
                                    }) |
                                    std::ranges::to<std::vector<bool>>();
    if (std::ranges::none_of(mask, std::identity{}))
        return {};
    return mask;
}

std::vector<SynopsisEdit>
twin_cut_edits(const clang::FunctionDecl* fn, const clang::SourceManager& sm, const clang::LangOptions& lang_opts) {
    const std::vector<bool> mask = twin_cut_mask(fn, sm, lang_opts);
    if (mask.empty())
        return {};
    const std::vector<const clang::Expr*> leaves = trailing_conjuncts(fn);
    const auto begin_of = [&sm](const clang::Expr* e) { return sm.getDecomposedLoc(e->getBeginLoc()).second; };
    const auto end_of   = [&](const clang::Expr* e) {
        return sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(e->getEndLoc(), 0, sm, lang_opts)).second;
    };
    if (std::ranges::all_of(mask, std::identity{})) {
        // The raw lexer reports a keyword as a raw identifier.
        const std::optional<clang::Token> keyword =
            clang::Lexer::findPreviousToken(leaves.front()->getBeginLoc(), sm, lang_opts, /*IncludeComments=*/false);
        if (!keyword || !keyword->is(clang::tok::raw_identifier) || keyword->getRawIdentifier() != "requires")
            return {};
        return {SynopsisEdit{sm.getDecomposedLoc(keyword->getLocation()).second, end_of(leaves.back()), ""}};
    }
    // A cut conjunct with a kept one before it takes the `&&` on its left; a
    // leading run of cut conjuncts takes the `&&` on its right instead, and
    // leaves a space, since `requires(` needs none before its parenthesis
    // and the kept conjunct after it may. The ranges abut rather than
    // overlap, so every one of them applies.
    const std::size_t first_kept = static_cast<std::size_t>(std::ranges::find(mask, false) - mask.begin());
    return std::views::iota(std::size_t{0}, leaves.size()) |
           std::views::filter([&mask](std::size_t i) { return mask[i]; }) | std::views::transform([&](std::size_t i) {
               return i < first_kept ? SynopsisEdit{begin_of(leaves[i]), begin_of(leaves[i + 1]), " "}
                                     : SynopsisEdit{end_of(leaves[i - 1]), end_of(leaves[i]), ""};
           }) |
           std::ranges::to<std::vector>();
}

} // namespace beman::specgen::frontend
