// src/beman/specgen/frontend/declaration_masks.cpp               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/declaration_masks.hpp>

#include <beman/specgen/markers.hpp>
#include <beman/specgen/foundation/enumeration.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/TypeBase.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/TokenKinds.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Token.h>
#include <llvm/Support/Casting.h>

#include <optional>

namespace beman::specgen::frontend {

namespace foundation = beman::specgen::foundation;

std::optional<AliasMask> alias_mask(const grammar::Markers& markers) {
    if (markers.impdef)
        return AliasMask::ImplDefined;
    if (markers.seebelow && !markers.seebelow_target)
        return AliasMask::SeeBelow;
    return std::nullopt;
}

clang::SourceLocation alias_last_token(const clang::TypeAliasDecl* alias,
                                       const clang::SourceManager& sm,
                                       const clang::LangOptions&   lang_opts) {
    const clang::SourceLocation given = alias->getEndLoc();
    clang::SourceLocation       last  = given;
    // substrate generic algorithm: a forward token walk to the declaration's
    // own `;`, whose distance is not known in advance; findNextToken advances
    // every step and gives out at end of file, so this terminates.
    for (std::optional<clang::Token> tok = clang::Lexer::findNextToken(last, sm, lang_opts); tok;
         tok                             = clang::Lexer::findNextToken(last, sm, lang_opts)) {
        if (tok->is(clang::tok::semi))
            return last;
        last = tok->getLocation();
    }
    return given;
}

std::optional<clang::SourceRange> alias_rhs_source_range(const clang::TypeAliasDecl* alias,
                                                         const clang::SourceManager& sm,
                                                         const clang::LangOptions&   lang_opts) {
    const clang::TypeSourceInfo* source = alias->getTypeSourceInfo();
    if (source == nullptr)
        return std::nullopt;
    clang::SourceRange range = source->getTypeLoc().getSourceRange();
    // QualifiedTypeLoc begins at its unqualified child on this Clang, so a
    // written leading `const` is not necessarily in getSourceRange(). Anchor
    // the end in TypeLoc, then recover the first RHS token from the `=` that
    // follows the alias identifier.
    const std::optional<clang::Token> equal = clang::Lexer::findNextToken(alias->getLocation(), sm, lang_opts);
    if (equal && equal->is(clang::tok::equal)) {
        const std::optional<clang::Token> first = clang::Lexer::findNextToken(equal->getLocation(), sm, lang_opts);
        if (first)
            range.setBegin(first->getLocation());
    }
    // ... and the last RHS token from the `;`, for the mirror-image reason.
    range.setEnd(alias_last_token(alias, sm, lang_opts));
    return range.isValid() ? std::optional{range} : std::nullopt;
}

std::optional<clang::SourceRange>
definition_mask_range(const clang::Decl* decl, const clang::SourceManager& sm, const clang::LangOptions& lang_opts) {
    const clang::TypeAliasDecl* alias = llvm::dyn_cast<clang::TypeAliasDecl>(decl);
    if (const auto* alias_tmpl = llvm::dyn_cast<clang::TypeAliasTemplateDecl>(decl))
        alias = alias_tmpl->getTemplatedDecl();
    if (alias != nullptr)
        return alias_rhs_source_range(alias, sm, lang_opts);
    if (const auto* concept_decl = llvm::dyn_cast<clang::ConceptDecl>(decl)) {
        const clang::Expr* constraint = concept_decl->getConstraintExpr();
        if (constraint != nullptr && constraint->getSourceRange().isValid())
            return constraint->getSourceRange();
    }
    return std::nullopt;
}

std::optional<SeeBelowTarget> seebelow_target(const grammar::Markers& markers) {
    if (!markers.seebelow)
        return std::nullopt;
    if (!markers.seebelow_target)
        return SeeBelowTarget::ReturnType;
    if (*markers.seebelow_target == "noexcept")
        return SeeBelowTarget::Noexcept;
    if (*markers.seebelow_target == "explicit")
        return SeeBelowTarget::Explicit;
    return std::nullopt; // parse_docblock has already diagnosed the unknown target
}

std::optional<clang::SourceRange> seebelow_source_range(const clang::FunctionDecl* fn, SeeBelowTarget target) {
    const clang::SourceRange valid = foundation::match(
        target,
        foundation::when<SeeBelowTarget::ReturnType>([&]() -> clang::SourceRange {
            const clang::SourceRange leading = fn->getReturnTypeSourceRange();
            if (leading.isValid())
                return leading;
            // getReturnTypeSourceRange() deliberately rejects a range that
            // sits after the declarator name — which is exactly an explicit
            // trailing return type, so `\seebelow` silently did nothing for
            // the SFINAE-friendly spellings most in need of masking
            // (issue #6). Mask the trailing type itself, read off the
            // FunctionTypeLoc: the declaration keeps its trailing shape and
            // renders `auto f(...) -> see below;`.
            if (const auto* proto = fn->getType()->getAs<clang::FunctionProtoType>();
                proto != nullptr && proto->hasTrailingReturn())
                if (const clang::TypeSourceInfo* tsi = fn->getTypeSourceInfo())
                    if (const auto ftl = tsi->getTypeLoc().IgnoreParens().getAs<clang::FunctionTypeLoc>())
                        return ftl.getReturnLoc().getSourceRange();
            return {};
        }),
        foundation::when<SeeBelowTarget::Noexcept>([&]() -> clang::SourceRange {
            if (const auto* type = fn->getType()->getAs<clang::FunctionProtoType>())
                if (const clang::Expr* expr = type->getNoexceptExpr())
                    return expr->getSourceRange();
            return {};
        }),
        foundation::when<SeeBelowTarget::Explicit>([&]() -> clang::SourceRange {
            if (const clang::Expr* expr = clang::ExplicitSpecifier::getFromDecl(fn).getExpr())
                return expr->getSourceRange();
            return {};
        }));
    return valid.isValid() ? std::optional{valid} : std::nullopt;
}

} // namespace beman::specgen::frontend
