// include/beman/specgen/frontend/constraints.hpp                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The Constraints derivation (design §5.1), and the conjunct phrasing the
// Mandates derivations share with it.
// [structure.specifications] read backwards: Constraints = removed from
// overload resolution = the declaration's requires-clauses. Source is both
// positions a clause can be written in -- on the template parameter list and
// trailing the declarator -- conjoined in [temp.constr.decl]/3 order. They are
// equivalent C++, so which one the author picks must not decide whether the
// spec is derived or retyped by hand (issue #119).
//
// A *constrained template parameter* (`template <integral T>`) and its
// abbreviated `auto` spelling are deliberately not a source: the draft shows
// those in the declaration rather than restating them as prose, so turning
// them into a Constraints element would diverge from the wording being
// generated. Sema normalization is also the wrong instrument for any of this,
// since it discards the written spellings expr_code_rewritten reads back.

#ifndef BEMAN_SPECGEN_FRONTEND_CONSTRAINTS_HPP
#define BEMAN_SPECGEN_FRONTEND_CONSTRAINTS_HPP

#include <beman/specgen/document_build.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/span_sentinels.hpp>
#include <beman/specgen/synopsis_edits.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Expr.h>
#include <clang/AST/Stmt.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceManager.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace beman::specgen::frontend {

beman::specgen::ir::CodeText expr_code_rewritten(const clang::Expr*                               e,
                                                 const clang::SourceManager&                      sm,
                                                 const clang::LangOptions&                        lang_opts,
                                                 const std::set<std::string>&                     ns_drop_set,
                                                 const std::map<const clang::Decl*, std::string>& expos_set,
                                                 const std::map<std::string, std::string>&        expos_names = {});

// Phrase one conjunct (design §5.1), peeled past parens/implicit casts:
//   - a single-argument concept-id reads "`X` models \libconcept{C}" (the WG21
//     spelling), X being the concept's sole template argument;
//   - a `!`-negation reads "`X` is `false`" (X being the negated subexpression,
//     without the `!`);
//   - anything else — a bool trait/variable, a zero-/multi-argument concept
//     (the single-subject "models" spelling does not apply cleanly), or an
//     unrecognized/verbatim expression such as a parenthesized disjunction —
//     reads "`X` is `true`" (or "is satisfied" for a many-arg concept).
beman::specgen::ir::Paragraph phrase_conjunct(const clang::Expr*                               leaf,
                                              const clang::SourceManager&                      sm,
                                              const clang::LangOptions&                        lang_opts,
                                              const std::set<std::string>&                     ns_drop_set,
                                              const std::map<const clang::Decl*, std::string>& expos_set);

// Derive a Constraints element from `fn`'s own requires-clauses -- the one on
// its template parameter list first, then the trailing one, the order
// [temp.constr.decl]/3 conjoins them in -- or nullopt if it has neither (or
// they somehow split into no conjuncts at all). Rendering (sentence vs.
// itemize) is conjuncts::render_into, shared with Mandates (design §5.3) — not
// reimplemented here.
std::optional<beman::specgen::ir::DescriptionElement>
derive_constraints(const clang::FunctionDecl*                       fn,
                   const clang::SourceManager&                      sm,
                   const clang::LangOptions&                        lang_opts,
                   const std::set<std::string>&                     ns_drop_set,
                   const std::map<const clang::Decl*, std::string>& expos_set);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_CONSTRAINTS_HPP
