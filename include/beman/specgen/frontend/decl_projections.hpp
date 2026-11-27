// include/beman/specgen/frontend/decl_projections.hpp            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Projections of a clang::Decl the extraction passes share: the function a
// declaration is, through a function template, and which of an entity's
// declarations it is.

#ifndef BEMAN_SPECGEN_FRONTEND_DECL_PROJECTIONS_HPP
#define BEMAN_SPECGEN_FRONTEND_DECL_PROJECTIONS_HPP

#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/Basic/SourceManager.h>

#include <utility>

namespace beman::specgen::frontend {

// The out-of-line definition of a class member, or null.  `isOutOfLine` alone
// says yes for a hidden friend, which is lexically in the class and has no
// in-class declaration to inherit a group from.
const clang::FunctionDecl* as_out_of_line_member(const clang::Decl* decl);

// The function a declaration is, looking through a function template.
const clang::FunctionDecl* function_or_template(const clang::Decl* decl);

// Is this the declaration of its entity that a synopsis should show?
//
// A synopsis lists an entity once.  In a document that is one file the question
// never arose -- out-of-line definitions live after the `/// END` fence, and a
// region held declarations only -- but a followed header (issue #77) carries
// both, and a definition folded in would list a member the class synopsis
// above it already declares, in the `Class::member` spelling the draft never
// prints.  Its *wording* still travels: only the synopsis entry is dropped.
//
// "First" is home_declaration's: a friend declaration that a namespace-scope
// declaration also declares does not count (issue #441).
bool is_first_declaration(const clang::Decl* decl, const clang::SourceManager& sm);

// The declaration a function's synopsis line and item declaration come from:
// its earliest declaration that is not a friend declaration in some class
// (issue #441). A class befriends a function -- a factory that reaches a
// private constructor -- without making the class the function's home, so
// the namespace-scope declaration is the one the draft shows, without
// `friend`. A function declared only as a friend, a hidden friend, has no
// other declaration and keeps its friend declaration.
const clang::FunctionDecl* home_declaration(const clang::FunctionDecl* fn, const clang::SourceManager& sm);

// Is `member` a class's friend declaration, not a definition, of a function
// whose home_declaration is elsewhere? Such a declaration grants access and
// nothing more: it has no line in the class synopsis and no entry in the
// class's coverage roster, whatever its markup (issue #441). The function is
// specified where it is declared.
bool redeclared_friend(const clang::Decl* member, const clang::SourceManager& sm);

//
// collect_inclass_items below and the build_omit_set/build_expos_set/
// build_seebelow_map pre-passes further down each walk a class's members
// looking for a function or a hidden friend, or walk `decls` looking for an
// out-of-line function definition or a class/class-template definition. Their
// own comments already say so ("Same pre-pass shape as build_omit_set"). The
// projections below are that repeated dyn_cast unwrapping, named once each;
// the caller-specific predicate (does the docblock say \omit, \expos,
// \seebelow; does the member carry a real body) stays local to each caller,
// since that is the part that is not actually shared.

// The out-of-line function a top-level decl defines: itself if a
// FunctionDecl, or a FunctionTemplateDecl's templated declaration.
// build_expos_set has no out-of-line case of its own (exposition-only
// applies only to members), so only build_omit_set and build_seebelow_map
// use this.
const clang::FunctionDecl* as_out_of_line_function(const clang::Decl* decl);

// A specialization and the primary it specializes, or {decl, nullptr} when
// `decl` specializes nothing. Partial and explicit alike: the partial kinds
// derive from these, so one cast each covers both (issue #49).
std::pair<const clang::Decl*, const clang::Decl*> specialized_primary(const clang::Decl* decl);

// A class or class template's templated record, if `decl` names either.
// Shared by all three set-builder pre-passes' outer walk.
const clang::CXXRecordDecl* as_record_decl(const clang::Decl* decl);

// The function a member decl names without unwrapping a function template.
// build_seebelow_map uses this as its first projection before handling a plain
// FunctionTemplateDecl explicitly.
const clang::FunctionDecl* member_as_function_or_friend(const clang::Decl* member);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_DECL_PROJECTIONS_HPP
