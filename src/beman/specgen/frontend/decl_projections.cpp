// src/beman/specgen/frontend/decl_projections.cpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/decl_projections.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclFriend.h>
#include <clang/AST/DeclTemplate.h>
#include <llvm/Support/Casting.h>

#include <clang/Basic/SourceManager.h>

#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

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

const clang::FunctionDecl* home_declaration(const clang::FunctionDecl* fn, const clang::SourceManager& sm) {
    const clang::FunctionDecl* first = fn->getFirstDecl();
    if (first->getFriendObjectKind() == clang::Decl::FOK_None)
        return first;
    const auto befriended = [](const clang::FunctionDecl* redecl) {
        const clang::Decl* as_written = redecl->getDescribedFunctionTemplate() != nullptr
                                            ? static_cast<const clang::Decl*>(redecl->getDescribedFunctionTemplate())
                                            : redecl;
        return as_written->getFriendObjectKind() != clang::Decl::FOK_None;
    };
    std::vector<const clang::FunctionDecl*> homes;
    std::ranges::copy_if(first->redecls(), std::back_inserter(homes), [&](const clang::FunctionDecl* redecl) {
        return !befriended(redecl);
    });
    const auto earliest =
        std::ranges::min_element(homes, [&](const clang::FunctionDecl* a, const clang::FunctionDecl* b) {
            return sm.isBeforeInTranslationUnit(a->getLocation(), b->getLocation());
        });
    return earliest == homes.end() ? first : *earliest;
}

bool redeclared_friend(const clang::Decl* member, const clang::SourceManager& sm) {
    const auto* friend_decl = llvm::dyn_cast<clang::FriendDecl>(member);
    if (friend_decl == nullptr)
        return false;
    const clang::FunctionDecl* fn = function_or_template(friend_decl->getFriendDecl());
    return fn != nullptr && !fn->doesThisDeclarationHaveABody() && home_declaration(fn, sm) != fn;
}

bool is_first_declaration(const clang::Decl* decl, const clang::SourceManager& sm) {
    if (const auto* fn = llvm::dyn_cast<clang::FunctionDecl>(decl))
        return home_declaration(fn, sm) == fn;
    if (const auto* tmpl = llvm::dyn_cast<clang::FunctionTemplateDecl>(decl))
        return home_declaration(tmpl->getTemplatedDecl(), sm) == tmpl->getTemplatedDecl();
    if (const auto* var = llvm::dyn_cast<clang::VarDecl>(decl))
        return var->getFirstDecl() == var;
    return true;
}

const clang::FunctionDecl* as_out_of_line_function(const clang::Decl* decl) {
    if (const auto* fn = llvm::dyn_cast<clang::FunctionDecl>(decl))
        return fn;
    if (const auto* ft = llvm::dyn_cast<clang::FunctionTemplateDecl>(decl))
        return ft->getTemplatedDecl();
    return nullptr;
}

std::pair<const clang::Decl*, const clang::Decl*> specialized_primary(const clang::Decl* decl) {
    if (const auto* record = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(decl))
        return {decl, record->getSpecializedTemplate()};
    if (const auto* variable = llvm::dyn_cast<clang::VarTemplateSpecializationDecl>(decl))
        return {decl, variable->getSpecializedTemplate()};
    return {decl, nullptr};
}

const clang::CXXRecordDecl* as_record_decl(const clang::Decl* decl) {
    if (const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(decl))
        return record;
    if (const auto* ct = llvm::dyn_cast<clang::ClassTemplateDecl>(decl))
        return ct->getTemplatedDecl();
    return nullptr;
}

const clang::FunctionDecl* member_as_function_or_friend(const clang::Decl* member) {
    if (const auto* friend_decl = llvm::dyn_cast<clang::FriendDecl>(member))
        return llvm::dyn_cast_or_null<clang::FunctionDecl>(friend_decl->getFriendDecl());
    return llvm::dyn_cast<clang::FunctionDecl>(member);
}

} // namespace beman::specgen::frontend
