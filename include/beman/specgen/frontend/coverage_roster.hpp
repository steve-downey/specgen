// include/beman/specgen/frontend/coverage_roster.hpp             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The coverage roster (design §9): every entity a synopsis declares, and
// what became of it.

#ifndef BEMAN_SPECGEN_FRONTEND_COVERAGE_ROSTER_HPP
#define BEMAN_SPECGEN_FRONTEND_COVERAGE_ROSTER_HPP

#include <beman/specgen/document_build.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/frontend/attach.hpp>
#include <beman/specgen/frontend/declaration_masks.hpp>
#include <beman/specgen/frontend/document_files.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceManager.h>

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

// Enumerate `record`'s class-body declarations and what became of each one
// (design §9). A synopsis is formatted code text by
// the time it leaves this tier, so nothing downstream can enumerate the
// declarations inside it; this roster carries that enumeration alongside the
// text, which is what lets the coverage invariant be *checked* in Tier A, with
// no Clang in the build (validate/validate.cpp).
//
// Observations only — the rule is not applied here. In particular
// `Undocumented` means "the front end found nothing accounting for this
// member", not "this is an error"; deciding that is the validator's job.
//
// `pending` is what collect_inclass_items just produced for this same class,
// which is where two facts come from that are otherwise unrecoverable: whether
// an in-class member's markup actually yielded an itemdescr (a `= default`
// member with a contentless docblock does not), and the stable name its
// wording was *routed* to. That routing is the only record that it happened at
// all — document_build::build_tree drops a member routed to a stable name no
// `\rSec` opens, leaving nothing else behind to see.
//
// Unmarked nested types, aliases and class-head `static_assert`s get no entry.
// A marked type alias is different: it has routed wording, and the roster
// records that observation as MemberKind::Alias.
std::vector<beman::specgen::ir::SynopsisEntry>
build_roster(const clang::CXXRecordDecl*                                     record,
             const clang::SourceManager&                                     sm,
             const std::map<const clang::Decl*, std::string>&                expos_set,
             const SourceMarkupMap&                                          source_markup,
             const std::vector<beman::specgen::document_build::PendingItem>& pending);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_COVERAGE_ROSTER_HPP
