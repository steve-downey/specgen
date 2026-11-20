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
#include <clang/AST/Stmt.h>
#include <clang/AST/TypeBase.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/TokenKinds.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Token.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>

#include <cctype>
#include <cstddef>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

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

std::optional<OmitTarget> omit_target(const grammar::Markers& markers) {
    if (!markers.omit_target)
        return std::nullopt;
    if (*markers.omit_target == "noexcept")
        return OmitTarget::Noexcept;
    if (*markers.omit_target == "default")
        return OmitTarget::Default;
    return std::nullopt; // parse_docblock has already diagnosed the unknown target
}

std::optional<DeclarationMask> declaration_mask(const grammar::Markers& markers) {
    const DeclarationMask mask{seebelow_target(markers), omit_target(markers)};
    if (mask == DeclarationMask{})
        return std::nullopt;
    return mask;
}

std::optional<std::pair<unsigned, unsigned>> omit_byte_range(const clang::FunctionDecl*  fn,
                                                             OmitTarget                  target,
                                                             const clang::SourceManager& sm,
                                                             const clang::LangOptions&   lang_opts) {
    bool                  invalid = false;
    const clang::FileID   file    = sm.getFileID(sm.getFileLoc(fn->getLocation()));
    const llvm::StringRef buffer  = sm.getBufferData(file, &invalid);
    if (invalid)
        return std::nullopt;
    const auto offset    = [&](clang::SourceLocation loc) { return sm.getDecomposedLoc(sm.getFileLoc(loc)).second; };
    const auto token_end = [&](clang::SourceLocation loc) {
        return offset(clang::Lexer::getLocForEndOfToken(sm.getFileLoc(loc), 0, sm, lang_opts));
    };
    const std::optional<std::pair<unsigned, unsigned>> part = foundation::match(
        target,
        foundation::when<OmitTarget::Noexcept>([&]() -> std::optional<std::pair<unsigned, unsigned>> {
            const clang::SourceRange range = fn->getExceptionSpecSourceRange();
            if (range.isInvalid())
                return std::nullopt;
            return std::pair{offset(range.getBegin()), token_end(range.getEnd())};
        }),
        foundation::when<OmitTarget::Default>([&]() -> std::optional<std::pair<unsigned, unsigned>> {
            const clang::SourceLocation keyword = fn->getDefaultLoc();
            if (keyword.isInvalid())
                return std::nullopt;
            // The `=` before the keyword, across blanks.
            const std::size_t equal = buffer.find_last_not_of(" \t\n", offset(keyword) - 1);
            if (equal == llvm::StringRef::npos || buffer[equal] != '=')
                return std::nullopt;
            return std::pair{static_cast<unsigned>(equal), token_end(keyword)};
        }));
    if (!part || part->second <= part->first)
        return std::nullopt;
    // The blanks before the part on its line go with it.
    const std::size_t kept = buffer.find_last_not_of(" \t", part->first - 1);
    return std::pair{kept == llvm::StringRef::npos ? 0U : static_cast<unsigned>(kept + 1), part->second};
}

std::vector<std::pair<unsigned, unsigned>> parameter_name_ranges(const clang::FunctionDecl*  fn,
                                                                 const clang::SourceManager& sm,
                                                                 const clang::LangOptions&   lang_opts) {
    if (fn->getLocation().isMacroID() || fn->getEndLoc().isMacroID())
        return {};
    bool                  invalid = false;
    const llvm::StringRef buffer  = sm.getBufferData(sm.getFileID(fn->getLocation()), &invalid);
    if (invalid)
        return {};
    const unsigned decl_begin = sm.getDecomposedLoc(fn->getBeginLoc()).second;
    // Through the declarator, not into a body: a use there is not the
    // declaration's.
    const clang::Stmt* body = fn->doesThisDeclarationHaveABody() && !fn->isDefaulted() ? fn->getBody() : nullptr;
    const unsigned     decl_end =
        body != nullptr && body->getBeginLoc().isFileID()
            ? sm.getDecomposedLoc(body->getBeginLoc()).second
            : sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(fn->getEndLoc(), 0, sm, lang_opts)).second;
    const llvm::StringRef declaration = buffer.substr(decl_begin, decl_end - decl_begin);
    const auto is_word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; };
    // How often `name` stands as a whole word in the declaration.
    const auto uses = [&](llvm::StringRef name) {
        std::size_t count = 0;
        // substrate generic algorithm: a cursor scan over the text for whole-word matches.
        for (std::size_t at = declaration.find(name); at != llvm::StringRef::npos; at = declaration.find(name, at + 1))
            if ((at == 0 || !is_word(declaration[at - 1])) &&
                (at + name.size() == declaration.size() || !is_word(declaration[at + name.size()])))
                ++count;
        return count;
    };
    return fn->parameters() | std::views::filter([&](const clang::ParmVarDecl* parameter) {
               return parameter->getIdentifier() != nullptr && parameter->getLocation().isFileID() &&
                      uses(parameter->getName()) == 1;
           }) |
           std::views::transform([&](const clang::ParmVarDecl* parameter) {
               const unsigned    name_begin = sm.getDecomposedLoc(parameter->getLocation()).second;
               const unsigned    name_end   = name_begin + static_cast<unsigned>(parameter->getName().size());
               const std::size_t kept       = buffer.find_last_not_of(" \t", name_begin - 1);
               return std::pair{kept == llvm::StringRef::npos ? 0U : static_cast<unsigned>(kept + 1), name_end};
           }) |
           std::ranges::to<std::vector>();
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
