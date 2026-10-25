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
bool is_first_declaration(const clang::Decl* decl);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_DECL_PROJECTIONS_HPP
