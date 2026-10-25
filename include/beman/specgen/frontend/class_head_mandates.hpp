// include/beman/specgen/frontend/class_head_mandates.hpp         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// A class's own description (design §5.2, §6): Mandates derived from the
// static_asserts at the head of its body, and the class docblock's elements.

#ifndef BEMAN_SPECGEN_FRONTEND_CLASS_HEAD_MANDATES_HPP
#define BEMAN_SPECGEN_FRONTEND_CLASS_HEAD_MANDATES_HPP

#include <beman/specgen/document_build.hpp>
#include <beman/specgen/frontend/docblock_lookup.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/lower.hpp>
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

// The description half of a class or class-template definition's own
// docblock (design §6, issue #18), folded into the SynopsisDecl the two
// definition arms of classify() build. `record` is the templated
// CXXRecordDecl for a template, so the derivation and the exposition-only
// spellings both come off the decl that carries the members.
//
// An authored `\mandates` replaces the derived class-scope paragraph and
// inherits its conjuncts, exactly as attach_function's attach_derivation does
// for a member (design §5.2): the derivation stays validator-only drift
// evidence rather than a second paragraph saying the same thing twice.
void attach_class_description(beman::specgen::document_build::SynopsisDecl&    out,
                              RecordDocblock&&                                 block,
                              const clang::CXXRecordDecl*                      record,
                              const clang::SourceManager&                      sm,
                              const clang::LangOptions&                        lang_opts,
                              const std::set<std::string>&                     ns_drop_set,
                              const std::map<const clang::Decl*, std::string>& expos_set);

// `\at` on a class definition's own docblock: where the class's own wording
// goes (issue #98, decision routed-wording-payload). Without it that wording
// stays beside the synopsis, which is the right default and the only thing
// #41 gave a folded-in class -- and which left the marker a silent no-op both
// inside a gathered region and outside one, since a SynopsisDecl had no
// routing channel of its own for the two nodes that are not its synopsis.
//
// It has one: `pending`, the same channel the class's members already route
// through, whose payload is an ir::Node exactly so these two can take it. Both
// nodes travel, keyed by the class's own offset, so they arrive in the target
// section in the order they would have stood in beside the synopsis --
// paragraph first, then description -- and ahead of any member of the same
// class routed to that section, whose key is further into the file.
//
// The roster entry is what makes the route *checkable*, the same way it is
// for a folded-in namespace entity: build_tree drops a pending item whose
// stable name no `\rSec` opens, and the entry is the only record that the
// request was made, which is what design §9's dangling-route rule reads. It
// names the class in both fields -- `name`, because the wording is the
// class's own, and `parent`, because a gathered header synopsis names no
// class and the entry is then the only thing that says which one a finding is
// about (issue #45). `kind` describes a member and a class is none of the
// three, so it keeps the default: only design §6's private-data nudge reads
// the field, and it reads it of a Private entry, never a Routed one.
//
// An `\at` on a class with no wording of its own routes nothing and is not
// reported: there is no description to lose, so nothing is being ignored.
void route_class_description(beman::specgen::document_build::SynopsisDecl&   out,
                             const clang::CXXRecordDecl*                     record,
                             const beman::specgen::lowering::ItemDirectives& directives);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_CLASS_HEAD_MANDATES_HPP
