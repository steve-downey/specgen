// include/beman/specgen/frontend/itemdecl.hpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Redeclaration-chain attachment (design §3.3): a documented function's or
// alias's itemdecl text.

#ifndef BEMAN_SPECGEN_FRONTEND_ITEMDECL_HPP
#define BEMAN_SPECGEN_FRONTEND_ITEMDECL_HPP

#include <beman/specgen/document_build.hpp>
#include <beman/specgen/frontend/declaration_masks.hpp>
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

// Itemdecl text renders from the in-class declaration, never the out-of-line
// definition (design §3.3): the out-of-line form carries `Class::`
// qualification, `inline`, and stacked template heads the draft never shows.
// `in_class` is ordinarily declaration-only for this tier's corpus (no body
// to splice), but defensively splice one to `;` the same way extract_synopsis
// does if it ever shows up, rather than emitting a definition as an itemdecl.
//
// `strip_requires_clause` is the default (design §5.1: "requires-clause
// is removed from the itemdecl"), and covers both positions a clause can be
// written in, since they are equivalent C++ (issue #119). A *trailing* clause
// is a truncation: cut the text at its own `getBeginLoc()` (the in-class
// decl's clause, since the itemdecl text is the in-class decl's own text —
// though per C++ it must match the out-of-line definition's), right-trim, and
// drop a trailing bare `requires` token left behind by the cut. A clause on
// the template parameter list is an excision instead, since it sits between
// the head and the declarator; that one is a dominant SynopsisEdit below.
// Callers pass false for a `\constraints-in-decl` declaration, which keeps
// either clause verbatim instead of deriving a Constraints element from it.
//
// `friend_begin` is the enclosing FriendDecl's begin location for a
// hidden friend: the `friend` keyword belongs to the FriendDecl, not the inner
// FunctionDecl (whose range starts at the return type), so extracting from it
// keeps `friend` in the itemdecl (the draft convention — cf. [expected.object.eq]).
// Invalid (the default) for an ordinary member, whose range begin is used as-is.
//
// `seebelow` (design §4.3) replaces the declared return type, or
// the operand of a conditional noexcept/explicit specifier, with a `\seebelow`
// span. The keyword and parentheses around a named operand remain authored.
beman::specgen::ir::CodeText extract_itemdecl(const clang::FunctionDecl*                       in_class,
                                              const clang::SourceManager&                      sm,
                                              const clang::LangOptions&                        lang_opts,
                                              bool                                             strip_requires_clause,
                                              const std::set<std::string>&                     ns_drop_set,
                                              const std::map<const clang::Decl*, std::string>& expos_set,
                                              clang::SourceLocation                            friend_begin = {},
                                              std::optional<SeeBelowTarget> seebelow = std::nullopt);

// A documented type alias is an ordinary itemdecl. The declaration
// itself is the AST-backed extraction range, while the TypeLoc identifies the
// RHS well enough for a dominant implementation-detail substitution. `head`
// is the enclosing TypeAliasTemplateDecl for a namespace-scope alias
// template, so the extraction starts at `template` rather than `using` —
// the same range-head override extract_synopsis takes for a class template.
beman::specgen::ir::CodeText extract_alias_itemdecl(const clang::TypeAliasDecl*                      alias,
                                                    const clang::SourceManager&                      sm,
                                                    const clang::LangOptions&                        lang_opts,
                                                    const std::set<std::string>&                     ns_drop_set,
                                                    const std::map<const clang::Decl*, std::string>& expos_set,
                                                    std::optional<AliasMask>                         mask,
                                                    const clang::Decl*                               head = nullptr);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_ITEMDECL_HPP
