// include/beman/specgen/frontend/inclass_items.hpp               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// In-class member collection (design §3.3 sub-case, §6): the documented
// members defined in a class body, and the `\ref` groups they sit under.

#ifndef BEMAN_SPECGEN_FRONTEND_INCLASS_ITEMS_HPP
#define BEMAN_SPECGEN_FRONTEND_INCLASS_ITEMS_HPP

#include <beman/specgen/document_build.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/lower.hpp>
#include <beman/specgen/span_sentinels.hpp>
#include <beman/specgen/synopsis_edits.hpp>
#include <beman/specgen/frontend/declaration_masks.hpp>
#include <beman/specgen/frontend/docblock_lookup.hpp>
#include <beman/specgen/frontend/file_views.hpp>
#include <beman/specgen/frontend/attach.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RawCommentList.h>
#include <clang/AST/Stmt.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

// The `\ref` group headers inside a class body, by offset -- the same class
// span and comment source extract_synopsis walks.  Two callers ask it the same
// question: collect_inclass_items, for a member declared and defined in the
// class, and the gathered-region fold, for the out-of-line definition of one,
// which carries the wording in the house style this tool was written for and
// belongs to its class's group like every other member (issue #77).
class ClassRefGroups {
  public:
    ClassRefGroups(const clang::CXXRecordDecl* record,
                   const clang::SourceManager& sm,
                   const clang::LangOptions&   lang_opts);

    // Stable name of the nearest `\ref` group at or before `offset`, else "".
    // groups_ is ascending, so the answer is the element just before the
    // partition point of "ref_offset <= offset" -- a binary search, not a
    // linear scan with a break.
    std::string section_for(unsigned offset) const;

  private:
    std::vector<std::pair<unsigned, std::string>> groups_;
};

// Collect the in-class function definitions and hidden friends that carry a
// `//!` docblock (design §3.3 sub-case, §6), including member function
// templates whose definition must remain in the class body.
// Each is attached from its in-class definition (markup, body, derivations, and
// itemdecl all come from the one decl) and appended to `pending` as a
// document_build::PendingItem, tagged with the stable name of the nearest
// preceding `\ref{...}` group in the class body — the section its wording
// belongs in. document_build::build_tree (stage 2) injects it when that
// section closes, ordered against out-of-line siblings by class-body offset.
//
// Skipped: members with no docblock; the bodyless in-class *declaration* of an
// out-of-line member (attached from its out-of-line definition instead, and
// reported when its own docblock asks for wording); data members (reported the
// same way); and `= default`/`= delete` and definition-less pure virtual
// members *unless* their docblock carries a description element or `\describe`
// (design §4.3, §6 — such entities get an itemdecl only when asked for). An `\omit`/`\merge` member is dropped here
// (no itemdescr; its synopsis line is removed by the omit-set pre-pass). Placement is the nearest preceding
// `\ref{...}` group, or `\at <stable>` when the docblock overrides it; a
// member routed to a stable name that names no `\rSec` section is stashed under
// a key no frame will close on and is silently dropped (a coverage
// concern for the validator).
//
// `diagnostics` receives every collected member's docblock findings,
// including those of members this pass then skips — an `\omit`ted member's
// markup can still be malformed. They ride the enclosing SynopsisDecl rather
// than the PendingItem for the same reason: a member dropped for want of a
// section must not take its diagnostics down with it.
void collect_inclass_items(const clang::CXXRecordDecl*                               record,
                           const clang::SourceManager&                               sm,
                           const clang::LangOptions&                                 lang_opts,
                           const std::set<std::string>&                              ns_drop_set,
                           const std::map<const clang::Decl*, std::string>&          expos_set,
                           const SkippedRanges&                                      skipped,
                           const SourceMarkupMap&                                    source_markup,
                           std::vector<beman::specgen::document_build::PendingItem>& pending,
                           std::vector<beman::specgen::document_build::Diagnostic>&  diagnostics);

void group_adjacent_aliases(std::vector<beman::specgen::document_build::PendingItem>& pending);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_INCLASS_ITEMS_HPP
