// include/beman/specgen/frontend/record_members.hpp              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The members of a class as the extraction passes see them: the real ones,
// with anonymous structs and unions flattened, the function a member
// declares, and where a member declaration's text ends.

#ifndef BEMAN_SPECGEN_FRONTEND_RECORD_MEMBERS_HPP
#define BEMAN_SPECGEN_FRONTEND_RECORD_MEMBERS_HPP

#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Stmt.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceManager.h>

#include <vector>

namespace beman::specgen::frontend {

// The enclosing class of a function: the semantic parent for a member, the
// lexical one for a hidden friend (semantically a namespace member).
const clang::CXXRecordDecl* enclosing_record(const clang::FunctionDecl* fn);

struct RealRecordMember {
    const clang::Decl* decl;
    bool               effectively_private;
    bool               nested_anonymous;
};

std::vector<RealRecordMember> real_record_members(const clang::CXXRecordDecl* record, bool enclosed_private = false);

// The function a class-body member declares, for the passes that must see
// *every* member function the way the synopsis does: a plain method, a member
// function template (a FunctionTemplateDecl in the member list, unwrapped so
// the canonical-decl-keyed marker sets match it), a hidden friend, or
// a hidden friend template. Null for anything else (a field, a nested type, an
// access label).
//
// This is the widest member projection: synopsis extraction, roster building,
// in-class attachment, and marker pre-passes use it when member templates must
// behave like plain members.
const clang::FunctionDecl* member_function_or_template(const clang::Decl* member);

// Where the spliced-away tail of an in-class function definition begins: the
// `{` of the body — or, for a constructor with a written mem-initializer
// list, the `:` that introduces it (issue #21). A ctor-initializer is
// implementation, never interface: the draft writes the declaration alone,
// and leaving the list in place also leaks the private member's spelling and
// the dropped-qualifier rewrite (`base_(move(base))`) into the synopsis and
// itemdecl. Starting the splice at the `:` removes the list with the body,
// and the removed range then suppresses the qualifier/expos edits inside it
// as with any spliced body. If the `:` cannot be found where it must be —
// directly before the first written initializer, across whitespace only —
// fall back to the body brace rather than cut at a guessed offset.
unsigned spliced_tail_begin(const clang::FunctionDecl* fn, const clang::Stmt* body, const clang::SourceManager& sm);

// The offset just past a class member declaration's terminating `;`: where a
// whole-declaration removal (`\merge`/`\omit`) has to stop, and where the
// `\freestanding` comment suffix is inserted (issues #85, #99).
//
// `getEndLoc()` does not reach it when the declaration carries a
// `= delete`/`= default` tail, for two independent reasons. Clang's parser
// (ParseCXXInlineMethods.cpp) extends a deleted or defaulted function's
// recorded end past the keyword only when the declarator Sema handed back is
// itself a FunctionDecl; for a member function *template* that declarator is
// the FunctionTemplateDecl, the cast fails silently, and the templated
// FunctionDecl's own end is left at the parameter list's closing `)` --
// never advanced at all (issue #85). And even where it is advanced, it stops
// at the keyword, so P2573's `= delete("reason")` leaves `("reason")`
// stranded behind the removal (issue #99).
//
// Reading the tail out of the source text does not repair either: it may be
// spelled through a macro -- `= BEMAN_EXPECTED_DELETE_MSG("...")`, which
// bemanproject/expected uses so a pre-C++26 build still gets a plain
// `delete` -- and raw-lexing forward then finds the macro's own identifier,
// never a keyword. So the *presence* of a tail is asked of the AST, whose
// answer Sema already resolved the macro for and which is therefore
// indifferent to spelling and to which preprocessor branch is live, and only
// its *extent* is lexed (decision construct-recognition).
//
// A member with no such tail keeps exactly the answer this returned before:
// the `;` right past the recorded end, or the end of that token when even
// that is not there (an in-class body, whose `}` no semicolon follows).
unsigned
member_declaration_end(const clang::Decl* decl, const clang::SourceManager& sm, const clang::LangOptions& lang_opts);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_RECORD_MEMBERS_HPP
