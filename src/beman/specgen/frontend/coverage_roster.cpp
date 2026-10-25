// src/beman/specgen/frontend/coverage_roster.cpp                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/coverage_roster.hpp>

#include <beman/specgen/document_build.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/lower.hpp>
#include <beman/specgen/frontend/attach.hpp>
#include <beman/specgen/frontend/docblock_lookup.hpp>
#include <beman/specgen/frontend/record_members.hpp>

#include <clang/AST/ASTConcept.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclFriend.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/NestedNameSpecifierBase.h>
#include <clang/AST/RawCommentList.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/TypeBase.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>
#include <llvm/Support/Casting.h>

#include <map>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

namespace {

// The decl a member's markup is written on: the out-of-line definition when
// there is one, else the member itself. This is why the roster needs no
// pre-pass of its own — `getDefinition()` reaches the definition from the
// in-class declaration directly, and by the time any of this runs the whole
// header has been parsed, so the redeclaration chain is complete.
const clang::FunctionDecl* markup_decl(const clang::FunctionDecl* fn) {
    const clang::FunctionDecl* def = fn->getDefinition();
    return def != nullptr ? def : fn;
}

} // namespace

std::vector<beman::specgen::ir::SynopsisEntry>
build_roster(const clang::CXXRecordDecl*                                     record,
             const clang::SourceManager&                                     sm,
             const std::map<const clang::Decl*, std::string>&                expos_set,
             const SourceMarkupMap&                                          source_markup,
             const std::vector<beman::specgen::document_build::PendingItem>& pending) {
    namespace ir = beman::specgen::ir;

    // The class-body offsets collect_inclass_items keyed its pending items on
    // (AttachedItem::inclass_offset), mapped to the section each was routed to.
    const std::map<unsigned, std::string> routed =
        pending | std::views::transform([](const beman::specgen::document_build::PendingItem& item) {
            return std::pair<unsigned, std::string>{item.offset, item.stable};
        }) |
        std::ranges::to<std::map<unsigned, std::string>>();

    std::vector<ir::SynopsisEntry> roster;

    // substrate generic algorithm: a fold into `roster` over `record`'s own
    // class-body order -- the same walk, in the same contractual order,
    // extract_synopsis and collect_inclass_items make, with a
    // first-match-wins ladder per member. A views pipeline would have to
    // reproduce the whole ladder inside its projection.
    for (const RealRecordMember& real_member : real_record_members(record)) {
        const clang::Decl* member = real_member.decl;
        if (llvm::isa<clang::AccessSpecDecl>(member))
            continue;

        // Anonymous records are transparent only for declarations that the
        // synopsis exposes. Their unmarked alternatives are not direct class
        // declarations and therefore are outside the coverage roster.
        if (real_member.nested_anonymous) {
            const auto* named = llvm::dyn_cast<clang::NamedDecl>(member);
            if (named == nullptr || !expos_set.contains(named->getCanonicalDecl()))
                continue;
        }

        // What this member declares, if it is something the draft would write
        // wording for: a function (however wrapped), a data member, or a
        // static data member. Anything else has no entry.
        const clang::FunctionDecl* fn    = member_function_or_template(member);
        const clang::NamedDecl*    named = fn;
        const auto*                alias = llvm::dyn_cast<clang::TypeAliasDecl>(member);
        // An unmarked alias is synopsis-only (design §6) — except a *private*
        // one, whose synopsis line is dropped: that one enters the roster as
        // Private, so the leakage checker can see a surviving declaration
        // naming it (`using type = raw;` naming the elided `raw`, issue #7) —
        // the same visibility question unmarked private data already answers.
        if (alias != nullptr && !has_docblock(alias, sm) && !real_member.effectively_private)
            continue;
        if (named == nullptr)
            named = alias;
        if (named == nullptr)
            named = llvm::dyn_cast<clang::FieldDecl>(member);
        if (named == nullptr)
            named = llvm::dyn_cast<clang::VarDecl>(member); // static data member
        if (named == nullptr)
            named = llvm::dyn_cast<clang::VarTemplateDecl>(member); // static data member template
        if (named == nullptr)
            continue; // unmarked nested type or static_assert: no wording of its own

        // The placement key collect_inclass_items keyed its pending items on
        // (AttachedItem::inclass_offset): the enclosing FriendDecl for a hidden
        // friend, whose `friend` keyword is not part of the inner FunctionDecl's
        // range, else the decl's own begin.
        const clang::Decl* key_decl  = llvm::isa<clang::FriendDecl>(member) ? member : named;
        const auto         routed_to = routed.find(sm.getDecomposedLoc(key_decl->getBeginLoc()).second);

        // Markup is read from the definition when there is one (a marker is
        // written where the wording is), else from the declaration itself. A
        // definition in a source file is in another parse, which read its
        // markers already (issue #149).
        const auto sourced = fn != nullptr ? source_markup.find(fn->getCanonicalDecl()) : source_markup.end();
        const beman::specgen::lowering::ItemDirectives directives =
            sourced != source_markup.end() ? sourced->second->attached.directives
                                           : docblock_directives(fn != nullptr ? markup_decl(fn) : named, sm);
        const clang::FunctionDecl* definition = fn != nullptr ? fn->getDefinition() : nullptr;

        ir::SynopsisEntry entry;
        entry.name = named->getNameAsString();
        // The same spelling the node's own `name` takes, and from the same
        // call: a gathered header synopsis holds several classes' entries and
        // names none of them, so this is what tells them apart (issue #45).
        entry.parent = record->getNameAsString();
        // `fn` is the FunctionDecl this member resolved to, so its absence is
        // exactly "this is a data member" -- a FieldDecl or a static VarDecl
        // or VarTemplateDecl, the only other things reaching here (a nested type, alias or
        // static_assert was skipped above). Design §6 treats unmarked private
        // data differently from an unmarked private *function*: the function
        // is silently omitted, the data earns a nudge, and this field is what
        // lets the Tier-A rule (validate.cpp) tell them apart.
        entry.kind = fn != nullptr      ? ir::MemberKind::Function
                     : alias != nullptr ? ir::MemberKind::Alias
                                        : ir::MemberKind::Data;

        // First match wins. The two "has wording" dispositions lead, so a
        // member that produced wording is accounted for however else it is
        // marked -- including a *private* one, which keeps its routed section
        // here even though extract_synopsis drops its synopsis line (design §6
        // deliberately does not special-case a documented private member).
        if (directives.merge) {
            entry.disposition = ir::Disposition::Merged;
        } else if (directives.omit || directives.elsewhere) {
            entry.disposition = ir::Disposition::Omitted;
        } else if (routed_to != routed.end()) {
            // In-class: placed by name. An empty target is the "under no
            // `\ref` group and no `\at`" case -- still routed, just to
            // nothing, which is why it must not read as Described.
            entry.disposition = ir::Disposition::Routed;
            entry.section     = routed_to->second;
        } else if (definition != nullptr && definition->isOutOfLine() && has_docblock(definition, sm)) {
            // Described out of line: the definition carries the markup and
            // lands in its lexically enclosing `\rSec` frame, so there is no
            // routed section to record.
            entry.disposition = ir::Disposition::Described;
        } else if (expos_set.contains(named->getCanonicalDecl())) {
            entry.disposition = ir::Disposition::Expos;
        } else if (real_member.effectively_private) {
            entry.disposition = ir::Disposition::Private;
        } else if (fn == nullptr && alias == nullptr) {
            // A *public* data member (issue #82).  Its type and its name are in
            // the synopsis and its class's own description says what it means,
            // which is how the draft specifies `from_chars_result`,
            // `ranges::in_out_result` and every other such struct: a data
            // member's specification is its declaration.  There is no itemdescr
            // to ask for, so demanding a description asks for something the
            // draft does not write. A static data member can have one, but
            // only by asking (issue #148), and then it is Routed above.
            entry.disposition = ir::Disposition::Declared;
        } else if (fn != nullptr && (fn->isDefaulted() || fn->isDeleted())) {
            entry.disposition = ir::Disposition::Defaulted;
        } else {
            entry.disposition = ir::Disposition::Undocumented;
        }

        roster.push_back(std::move(entry));
    }

    return roster;
}

} // namespace beman::specgen::frontend
