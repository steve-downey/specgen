// src/beman/specgen/frontend/record_members.cpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/record_members.hpp>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclFriend.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/RawCommentList.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/Specifiers.h>
#include <clang/Basic/TokenKinds.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Token.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>

#include <cstddef>
#include <optional>
#include <vector>

namespace beman::specgen::frontend {

namespace {

// Flatten only anonymous structs/unions. Clang represents an anonymous union
// with a nested record holding the source fields plus implicit FieldDecl and
// IndirectFieldDecl projections on the enclosing class. The projections have
// no source declaration of their own; walking the nested record gives each
// real member exactly once and leaves named nested classes as class members.
void append_real_record_members(const clang::CXXRecordDecl*    record,
                                bool                           enclosed_private,
                                bool                           nested_anonymous,
                                std::vector<RealRecordMember>& out) {
    // substrate generic algorithm: recursive tree flattening with inherited
    // access and provenance state; only anonymous record nodes are expanded.
    for (const clang::Decl* member : record->decls()) {
        if (member->isImplicit() || llvm::isa<clang::IndirectFieldDecl>(member))
            continue;
        // A static data member template's implicit instantiation, `spare_<int>`
        // for a use of it, is a member of the class too, though nothing in the
        // class body declares it (issue #172).
        if (const auto* instance = llvm::dyn_cast<clang::VarTemplateSpecializationDecl>(member);
            instance != nullptr && instance->getSpecializationKind() == clang::TSK_ImplicitInstantiation)
            continue;

        const bool effectively_private = enclosed_private || member->getAccess() == clang::AS_private;
        if (const auto* nested = llvm::dyn_cast<clang::CXXRecordDecl>(member);
            nested != nullptr && nested->isAnonymousStructOrUnion()) {
            append_real_record_members(nested, effectively_private, true, out);
            continue;
        }
        out.push_back(RealRecordMember{member, effectively_private, nested_anonymous});
    }
}

// The `;` that terminates a `= delete`/`= default` tail, found by raw-lexing
// forward from a declaration's recorded end to the first semicolon at
// bracket depth zero. The caller has already established from the AST that
// such a tail is there; this only measures how far it reaches, so everything
// crossed on the way belongs to it.
//
// Depth tracking is what makes the walk indifferent to how the tail is
// spelled: P2573's `= delete("reason")` and a macro invocation's argument
// list are both balanced parentheses to skip over, and the `;` is in the
// file's own text either way -- which is the whole reason the extent is
// lexed rather than derived from the expansion.
std::optional<clang::SourceLocation> deleted_or_defaulted_semi(clang::SourceLocation       end,
                                                               const clang::SourceManager& sm,
                                                               const clang::LangOptions&   lang_opts) {
    int depth = 0;
    // substrate generic algorithm: a token-at-a-time scan whose stepping
    // primitive, `Lexer::findNextToken`, is also its termination condition --
    // it returns nothing at end of file. There is no token range to fold
    // over, and the depth counter is state the walk carries, not a result it
    // accumulates.
    for (std::optional<clang::Token> tok = clang::Lexer::findNextToken(end, sm, lang_opts); tok.has_value();
         tok                             = clang::Lexer::findNextToken(tok->getLocation(), sm, lang_opts)) {
        if (tok->isOneOf(clang::tok::l_paren, clang::tok::l_square, clang::tok::l_brace))
            ++depth;
        else if (tok->isOneOf(clang::tok::r_paren, clang::tok::r_square, clang::tok::r_brace))
            --depth;
        else if (depth == 0 && tok->is(clang::tok::semi))
            return tok->getLocation();
    }
    return std::nullopt;
}

} // namespace

const clang::CXXRecordDecl* enclosing_record(const clang::FunctionDecl* fn) {
    if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(fn))
        return method->getParent();
    return llvm::dyn_cast_or_null<clang::CXXRecordDecl>(fn->getLexicalDeclContext());
}

std::vector<RealRecordMember> real_record_members(const clang::CXXRecordDecl* record, bool enclosed_private) {
    std::vector<RealRecordMember> members;
    append_real_record_members(record, enclosed_private, false, members);
    return members;
}

const clang::FunctionDecl* member_function_or_template(const clang::Decl* member) {
    if (const auto* fn = llvm::dyn_cast<clang::FunctionDecl>(member))
        return fn;
    if (const auto* fn_tmpl = llvm::dyn_cast<clang::FunctionTemplateDecl>(member))
        return fn_tmpl->getTemplatedDecl();
    if (const auto* friend_decl = llvm::dyn_cast<clang::FriendDecl>(member)) {
        if (const auto* fn = llvm::dyn_cast_or_null<clang::FunctionDecl>(friend_decl->getFriendDecl()))
            return fn;
        if (const auto* ft = llvm::dyn_cast_or_null<clang::FunctionTemplateDecl>(friend_decl->getFriendDecl()))
            return ft->getTemplatedDecl();
    }
    return nullptr;
}

unsigned spliced_tail_begin(const clang::FunctionDecl* fn, const clang::Stmt* body, const clang::SourceManager& sm) {
    const unsigned body_begin = sm.getDecomposedLoc(body->getBeginLoc()).second;
    const auto*    ctor       = llvm::dyn_cast<clang::CXXConstructorDecl>(fn);
    if (ctor == nullptr)
        return body_begin;

    clang::SourceLocation first_init;
    // substrate generic algorithm: a min-fold over the written initializers'
    // locations — CXXConstructorDecl::inits() is a pointer range, and the
    // written/implicit split plus the location projection has no std::ranges
    // name that reads better than the fold itself.
    for (const clang::CXXCtorInitializer* init : ctor->inits()) {
        if (!init->isWritten())
            continue;
        const clang::SourceLocation loc = init->getSourceRange().getBegin();
        if (first_init.isInvalid() || sm.getDecomposedLoc(loc).second < sm.getDecomposedLoc(first_init).second)
            first_init = loc;
    }
    if (first_init.isInvalid())
        return body_begin;

    const auto [init_file, init_begin] = sm.getDecomposedLoc(first_init);
    bool                  invalid      = false;
    const llvm::StringRef buffer       = sm.getBufferData(init_file, &invalid);
    if (invalid)
        return body_begin;
    const std::size_t colon = buffer.substr(0, init_begin).find_last_not_of(" \t\n\v\f\r");
    if (colon == llvm::StringRef::npos || buffer[colon] != ':')
        return body_begin;
    return static_cast<unsigned>(colon);
}

unsigned
member_declaration_end(const clang::Decl* decl, const clang::SourceManager& sm, const clang::LangOptions& lang_opts) {
    // A tail spelled through a macro can leave the recorded end inside the
    // expansion, which the raw lexer cannot walk. The file location is the
    // invocation in the source text, which is what the scans below read; for
    // every other declaration it is the recorded end unchanged.
    const clang::SourceLocation end = sm.getFileLoc(decl->getEndLoc());
    const clang::FunctionDecl*  fn  = member_function_or_template(decl);
    if (fn != nullptr && (fn->isDeletedAsWritten() || fn->isDefaulted())) {
        if (const std::optional<clang::SourceLocation> semi = deleted_or_defaulted_semi(end, sm, lang_opts))
            return sm.getDecomposedLoc(*semi).second + 1;
    }
    if (const std::optional<clang::Token> semi = clang::Lexer::findNextToken(end, sm, lang_opts);
        semi && semi->is(clang::tok::semi))
        return sm.getDecomposedLoc(semi->getLocation()).second + 1;
    return sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(end, 0, sm, lang_opts)).second;
}

} // namespace beman::specgen::frontend
