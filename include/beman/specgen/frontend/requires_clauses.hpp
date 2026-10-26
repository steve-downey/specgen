// include/beman/specgen/frontend/requires_clauses.hpp            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The requires-clauses a declaration is constrained by, in both positions
// they can be written ([temp.constr.decl]/3), and their top-level `&&`
// conjuncts. Read by the merged-twin cut, synopsis extraction, and the
// Constraints and Mandates derivations (design §5.1, §5.2).

#ifndef BEMAN_SPECGEN_FRONTEND_REQUIRES_CLAUSES_HPP
#define BEMAN_SPECGEN_FRONTEND_REQUIRES_CLAUSES_HPP

#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>

#include <vector>

namespace beman::specgen::frontend {

// Flatten `e`'s top-level `&&` conjuncts into `out`, in source order. Only
// `&&` is flattened — a `||` at the top level is left as a single leaf
// ("no flattening through disjunctions", design §5.1: `A && (B || C)` yields
// two conjuncts, the second verbatim).
void split_conjuncts(const clang::Expr* e, std::vector<const clang::Expr*>& out);

std::vector<const clang::Expr*> trailing_conjuncts(const clang::FunctionDecl* fn);

// A class template's own head: the parameter list carrying its requires-clause,
// its parameters' type constraints, and their default arguments. All of it
// belongs to the ClassTemplateDecl, not to the templated record, so a rewrite
// traversal rooted at the record never reaches it -- an exposition-only concept
// named in a class-head constraint kept its implementation spelling while the
// same concept in a member's requires-clause was renamed, in the same rendered
// block (issue #48). Rooting at the ClassTemplateDecl instead would walk the
// record a second time and emit every member's edits twice.
const clang::TemplateParameterList* head_parameters(const clang::Decl* head_decl);

// A function template's *own* head parameter list -- the one whose
// requires-clause constrains this function. Null for a non-template function,
// and for a non-template member of a class template: the enclosing class's
// clause constrains the class, not the member ([temp.constr.decl]/3 lists only
// the declaration's own template-parameter-list and trailing clause). For a
// member template it is the innermost list, the `template <class U>` the
// out-of-line definition writes under the class's own head.
const clang::TemplateParameterList* function_head_parameters(const clang::FunctionDecl* fn);

// The requires-clause written after that list, as opposed to the trailing one
// written after the declarator. The two positions are equivalent C++ and both
// feed design §5.1 (issue #119).
const clang::Expr* head_requires_clause(const clang::FunctionDecl* fn);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_REQUIRES_CLAUSES_HPP
