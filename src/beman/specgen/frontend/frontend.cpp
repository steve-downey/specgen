// src/beman/specgen/frontend/frontend.cpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/frontend.hpp>

#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/docblock.hpp>
#include <beman/specgen/document_build.hpp>
#include <beman/specgen/parse/cursor.hpp>
#include <beman/specgen/parse/parser.hpp>
#include <beman/specgen/frontend/attach.hpp>
#include <beman/specgen/frontend/build_failure.hpp>
#include <beman/specgen/frontend/class_head_mandates.hpp>
#include <beman/specgen/frontend/compile_args.hpp>
#include <beman/specgen/frontend/decl_projections.hpp>
#include <beman/specgen/frontend/declaration_masks.hpp>
#include <beman/specgen/frontend/docblock_lookup.hpp>
#include <beman/specgen/frontend/document_files.hpp>
#include <beman/specgen/frontend/draft_format.hpp>
#include <beman/specgen/frontend/expos_spellings.hpp>
#include <beman/specgen/frontend/file_views.hpp>
#include <beman/specgen/frontend/inclass_items.hpp>
#include <beman/specgen/frontend/parse_header.hpp>
#include <beman/specgen/frontend/qualifiers.hpp>
#include <beman/specgen/frontend/record_members.hpp>
#include <beman/specgen/frontend/synopsis_extraction.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/lower.hpp>
#include <beman/specgen/markers.hpp>
#include <beman/specgen/rsec.hpp>
#include <beman/specgen/section_markers.hpp>
#include <beman/specgen/span_sentinels.hpp>
#include <beman/specgen/synopsis_edits.hpp>

// GCC reports -Wnonnull eight times from inside LLVM's own headers when it
// inlines RecursiveASTVisitor::TraverseCXXRecordHelper for the visitors below:
// LazyOffsetPtr::get performs a pointer-to-member call on an
// ExternalASTSource* that is null whenever there is no PCH or module, which is
// exactly when the branch is not taken -- a precondition GCC cannot see
// through. -isystem does not silence it, because the diagnostic comes from the
// middle end after inlining rather than from the parse, so the suppression is
// scoped to the statements these headers contribute and our own code stays
// fully checked.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull"
#include <clang/AST/ASTConcept.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclFriend.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/ExprConcepts.h>
#include <clang/AST/NestedNameSpecifierBase.h>
#include <clang/AST/RawCommentList.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/TypeBase.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/FileEntry.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/TokenKinds.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/PreprocessingRecord.h>
#include <clang/Tooling/Core/Replacement.h>

#include <llvm/ADT/FoldingSet.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/Path.h>
#pragma GCC diagnostic pop

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace beman::specgen::frontend {

namespace parse = beman::specgen::parse;

using grammar::parse_ref;
using grammar::parse_rsec;
using grammar::rsec_tag_recognized;
using grammar::section_end_fence;
using grammar::SectionEndFence;
using grammar::SectionHeader;
using span_sentinels::recover_sentinels;
using span_sentinels::span_sentinel;
using span_sentinels::SpanInfo;
using synopsis_edits::add_dominant_edit;
using synopsis_edits::edits_overlap;
using synopsis_edits::sort_back_to_front;
using synopsis_edits::SynopsisEdit;

namespace {

namespace ir = beman::specgen::ir;

// One offset-ordered raw item: either a still-typed top-level decl or a
// comment's raw text, collected before classify() (the pipeline's first
// stage) converts it to a document_build::DocEvent. Mirrors SourceItem,
// but keeps the clang::Decl* (SourceItem only keeps a formatted label) so
// classify() can dispatch on it. Not to be confused with
// document_build::DocEvent, classify's clang-free output.
struct RawItem {
    unsigned     offset = 0;
    clang::Decl* decl   = nullptr; // non-null: a decl event
    std::string  comment_text;     // decl == nullptr: a comment event
};

// --- the coverage roster (design §9) ----------------------------------------

// The decl a member's markup is written on: the out-of-line definition when
// there is one, else the member itself. This is why the roster needs no
// pre-pass of its own — `getDefinition()` reaches the definition from the
// in-class declaration directly, and by the time any of this runs the whole
// header has been parsed, so the redeclaration chain is complete.
const clang::FunctionDecl* markup_decl(const clang::FunctionDecl* fn) {
    const clang::FunctionDecl* def = fn->getDefinition();
    return def != nullptr ? def : fn;
}

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

// The canonical declarations to drop from every synopsis (design §4.3):
// members whose markup carries `\omit` or `\merge`. Built as a pre-pass because
// a class's synopsis is extracted the moment the class is seen — before the
// out-of-line definition carrying the marker is reached in the event stream —
// so extract_synopsis needs the set up front. Scans both out-of-line
// definitions (keyed to their in-class declaration via getFirstDecl()) and
// in-class members; keys on getCanonicalDecl() so the check in extract_synopsis
// matches whichever redeclaration it walks.
std::set<const clang::Decl*> build_omit_set(const std::vector<clang::Decl*>& decls, const clang::SourceManager& sm) {
    const auto is_marked = [&sm](const clang::FunctionDecl* fn) {
        const beman::specgen::lowering::ItemDirectives dirs = docblock_directives(fn, sm);
        return suppresses_generated_entity(dirs);
    };

    // Out-of-line function definitions, keyed to their in-class declaration.
    std::set<const clang::Decl*> omit =
        decls | std::views::transform(as_out_of_line_function) | std::views::filter([](const clang::FunctionDecl* fn) {
            return fn != nullptr && fn->isThisDeclarationADefinition() && fn->isOutOfLine();
        }) |
        std::views::filter(is_marked) |
        std::views::transform([](const clang::FunctionDecl* fn) { return fn->getFirstDecl()->getCanonicalDecl(); }) |
        std::ranges::to<std::set<const clang::Decl*>>();

    // In-class members of a class or class template, including member function
    // templates. The trailing `ranges::to` owns each record's members across
    // the `transform(...) | join` below.
    const auto omit_members_of = [&is_marked](const clang::CXXRecordDecl* record) {
        return record->decls() |
               std::views::transform([](const clang::Decl* member) { return member_function_or_template(member); }) |
               std::views::filter([](const clang::FunctionDecl* fn) { return fn != nullptr; }) |
               std::views::filter(is_marked) |
               std::views::transform([](const clang::FunctionDecl* fn) { return fn->getCanonicalDecl(); }) |
               std::ranges::to<std::vector<const clang::Decl*>>();
    };
    omit.insert_range(decls | std::views::transform(as_record_decl) |
                      std::views::filter([](const clang::CXXRecordDecl* record) {
                          return record != nullptr && record->isThisDeclarationADefinition();
                      }) |
                      std::views::transform(omit_members_of) | std::views::join);

    return omit;
}

// A member's exposition-only name candidate: a data member, static or not, a
// type alias, or a method, including the underlying method of a
// FunctionTemplateDecl. A static data member and a static data member template
// used to be missing, so an `\expos` one never reached the expos set and its
// synopsis line was dropped as unmarked private data (issue #172).
// Hidden friends
// remain namespace functions rather than class exposition members.
const clang::NamedDecl* as_field_or_method(const clang::Decl* member) {
    if (const auto* fd = llvm::dyn_cast<clang::FieldDecl>(member))
        return fd;
    if (llvm::isa<clang::VarDecl, clang::VarTemplateDecl>(member))
        return llvm::cast<clang::NamedDecl>(member);
    if (const auto* alias = llvm::dyn_cast<clang::TypeAliasDecl>(member))
        return alias;
    if (const auto* md = llvm::dyn_cast_or_null<clang::CXXMethodDecl>(member_function_or_template(member)))
        return md;
    // A nested class (issue #80).  A view's iterator is one, private and named
    // by every `begin`, `end` and `operator++` signature the class publishes,
    // so it is exactly the shape `\expos` is for: rendered under an exposition
    // name rather than dropped as a private member the wording goes on naming.
    if (const auto* nested = llvm::dyn_cast<clang::CXXRecordDecl>(member);
        nested != nullptr && nested->getIdentifier() != nullptr)
        return nested;
    if (const auto* tmpl = llvm::dyn_cast<clang::ClassTemplateDecl>(member))
        return tmpl;
    return nullptr;
}

// Namespace-owned entities that can be rendered as free-standing exposition
// synopses. isFileContext includes the global namespace and named namespaces,
// while excluding an out-of-line definition of a static data member.
// Class templates belong here too (issue #23): the draft's exposition-only
// helpers are spelled as class templates as well as alias templates, and
// their template-id uses resolve through the same TemplateName hook the
// alias-template uses take. Plain classes too (issue #142), whose uses are
// TagTypeLocs.
bool is_namespace_expos_candidate(const clang::Decl* decl) {
    if (llvm::isa<clang::ConceptDecl>(decl) || llvm::isa<clang::VarTemplateDecl>(decl) ||
        llvm::isa<clang::TypeAliasTemplateDecl>(decl) || llvm::isa<clang::ClassTemplateDecl>(decl))
        return true;
    // An enumeration belongs here for the same reason it is a wording entity
    // at all (issue #68): its definition is its interface, so an
    // exposition-only one renders as a standalone synopsis under its exposid
    // name. Without this it took the *wording* path with an empty description,
    // which made it an `\also` follower and folded its declaration into
    // whatever item preceded it.
    if (const auto* enumeration = llvm::dyn_cast<clang::EnumDecl>(decl))
        return enumeration->getDeclContext()->isFileContext();
    // A specialization of one, which is the same entity as its primary and so
    // shares its disposition (issue #49). The variable-template kinds reach
    // the VarDecl test below on their own; a class-template specialization is
    // a CXXRecordDecl and would otherwise be routed as an ordinary class.
    if (llvm::isa<clang::ClassTemplateSpecializationDecl>(decl))
        return true;
    // A plain class, for the reason a class template is one (issue #142): the
    // draft's exposition-only helpers include non-template classes. A defined
    // one takes the ordinary class arms, like a defined class template; a
    // forward declaration of a marked class is reproduced as an exposition-only
    // declaration, since the synopsis needs it to compile (issue #151). The
    // templated record of a class template is that template's, and an
    // unnamed class has no name to rewrite.
    if (const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(decl))
        return record->getIdentifier() != nullptr && record->getDescribedClassTemplate() == nullptr &&
               record->getDeclContext()->isFileContext();
    if (const auto* alias = llvm::dyn_cast<clang::TypeAliasDecl>(decl))
        return alias->getDeclContext()->isFileContext();
    const auto* variable = llvm::dyn_cast<clang::VarDecl>(decl);
    return variable != nullptr && variable->getDeclContext()->isFileContext();
}

// A class definition among the candidates above: a class template whose
// templated record is defined, or a class-template specialization that is.
// Being exposition-only changes how such a class is *named* and nothing else,
// so it is extracted like any other class (design §3.4) rather than copied
// through as a free-standing declaration.
bool is_defined_record(const clang::Decl* decl) {
    if (const auto* tmpl = llvm::dyn_cast<clang::ClassTemplateDecl>(decl)) {
        const clang::CXXRecordDecl* templated = tmpl->getTemplatedDecl();
        return templated != nullptr && templated->isThisDeclarationADefinition();
    }
    const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(decl);
    return record != nullptr && record->isThisDeclarationADefinition();
}

// The documented-wording counterpart: the same namespace-owned entity kinds,
// asked about by a docblock with description content rather than `\expos` —
// ordinary wording items (design §6) instead of standalone synopses. The
// isFileContext guard excludes the out-of-line definition of a class's own
// member (a static data member, a member variable template), whose wording
// belongs to its class.
//
// An enumeration is one of them (issue #68). It is a type whose definition is
// its interface, so the declaration is the itemdecl and the docblock's
// description follows — the draft's shape for [fs.enum.file.type], whose
// wording is a two-column table of enumerator meanings and which `\lib2dtab2`
// already renders. It was the last kind a library could document and get
// nothing for: an error said so rather than dropping it silently, but an
// error is not a way to specify an enum, and there was no other.
const clang::NamedDecl* as_namespace_entity(const clang::Decl* decl) {
    const bool entity_kind = llvm::isa<clang::ConceptDecl>(decl) || llvm::isa<clang::VarTemplateDecl>(decl) ||
                             llvm::isa<clang::TypeAliasTemplateDecl>(decl) || llvm::isa<clang::TypeAliasDecl>(decl) ||
                             llvm::isa<clang::VarDecl>(decl) || llvm::isa<clang::EnumDecl>(decl);
    if (!entity_kind || !decl->getDeclContext()->isFileContext())
        return nullptr;
    return llvm::cast<clang::NamedDecl>(decl);
}

// The exposition-only members (design §4.3/§3.5): canonical member decl →
// its `\exposid` display name (`\expos(name)` override, else `exposid_name` of the
// identifier). A pre-pass like build_omit_set, so extract_synopsis has the set
// when a class is first seen. Covers data members, member functions, and the
// namespace-scope concepts/variables/variable templates/aliases/alias
// templates whose resolved uses are rewritten as exposids
// (extract_namespace_expos_synopsis emits their free-standing declarations).
std::map<const clang::Decl*, std::string> build_expos_set(const std::vector<clang::Decl*>& decls,
                                                          const clang::SourceManager&      sm) {
    using ExposEntry = std::pair<const clang::Decl*, std::string>;

    const auto is_marked  = [&sm](const clang::NamedDecl* named) { return docblock_directives(named, sm).expos; };
    const auto expos_name = [&sm](const clang::NamedDecl* named) {
        const beman::specgen::lowering::ItemDirectives dirs = docblock_directives(named, sm);
        return dirs.expos_name.value_or(named->getIdentifier() != nullptr
                                            ? beman::specgen::lowering::exposid_name(named->getName().str())
                                            : std::string{});
    };
    // Anonymous records are transparent here: their source members are the
    // candidates, while Clang's implicit injected projections are not.
    // The marked members of a record, and of the records nested in it: a
    // nested class's own state is exposition for the same reason its class's
    // is, and an extracted body that names it has to say the exposition name
    // (issue #80).
    const auto expos_members_of = [&](this const auto&            self,
                                      const clang::CXXRecordDecl* record) -> std::vector<ExposEntry> {
        std::vector<ExposEntry> out;
        // substrate generic algorithm: a filter-map over one record's members
        // that also descends -- the recursion is why this is a loop and not a
        // pipeline built in place.
        for (const RealRecordMember& member : real_record_members(record)) {
            if (const clang::NamedDecl* named = as_field_or_method(member.decl); named != nullptr && is_marked(named))
                out.emplace_back(named->getCanonicalDecl(), expos_name(named));
            if (const clang::CXXRecordDecl* nested = as_record_decl(member.decl);
                nested != nullptr && nested->isThisDeclarationADefinition())
                out.append_range(self(nested));
        }
        return out;
    };

    std::map<const clang::Decl*, std::string> expos;
    expos.insert_range(decls | std::views::transform(as_record_decl) |
                       std::views::filter([](const clang::CXXRecordDecl* record) {
                           return record != nullptr && record->isThisDeclarationADefinition();
                       }) |
                       std::views::transform(expos_members_of) | std::views::join);
    expos.insert_range(
        decls | std::views::filter(is_namespace_expos_candidate) |
        std::views::transform([](const clang::Decl* decl) { return llvm::cast<clang::NamedDecl>(decl); }) |
        std::views::filter(is_marked) | std::views::transform([&expos_name](const clang::NamedDecl* named) {
            return std::pair<const clang::Decl*, std::string>{named->getCanonicalDecl(), expos_name(named)};
        }));

    // A namespace using-declaration introduces no declaration of its own for
    // a synopsis to print, but `\expos` on it still marks the declarations it
    // makes reachable under that name. Map every shadow's target so a use in
    // this header -- or in a sibling header that includes it -- takes the
    // ordinary exposition-use rewrite path (issue #113).
    const auto expos_targets_of_using = [&](const clang::UsingDecl* using_decl) {
        const std::string display = expos_name(using_decl);
        return using_decl->shadows() | std::views::transform([display](const clang::UsingShadowDecl* shadow) {
                   return std::array{
                       ExposEntry{shadow->getCanonicalDecl(), display},
                       ExposEntry{shadow->getTargetDecl()->getCanonicalDecl(), display},
                   };
               }) |
               std::views::join | std::ranges::to<std::vector<ExposEntry>>();
    };
    expos.insert_range(
        decls | std::views::transform([](const clang::Decl* decl) { return llvm::dyn_cast<clang::UsingDecl>(decl); }) |
        std::views::filter([](const clang::UsingDecl* using_decl) { return using_decl != nullptr; }) |
        std::views::filter(is_marked) | std::views::transform(expos_targets_of_using) | std::views::join);

    // A second pass, because a specialization takes its name from the primary
    // and the primary need not have been seen first. It is the same entity, so
    // it cannot be exposition-only under one name and not under another
    // (issue #49) -- and its own markers do not enter into it: `\expos` on a
    // specialization was accepted and ignored, `\also` made it a wording item,
    // and neither kept the raw implementation spelling out of the wording.
    expos.insert_range(decls | std::views::transform(specialized_primary) |
                       std::views::filter([&expos](const auto& pair) {
                           return pair.second != nullptr && expos.contains(pair.second->getCanonicalDecl());
                       }) |
                       std::views::transform([&expos](const auto& pair) {
                           return std::pair<const clang::Decl*, std::string>{
                               pair.first->getCanonicalDecl(), expos.at(pair.second->getCanonicalDecl())};
                       }));
    return expos;
}

// Names intentionally supplied by the paper but absent from this document's
// rendered nodes (issue #113). `\elsewhere` is the explicit promise that the
// paper hand-authors a declaration which specgen must suppress. A `\expos`
// namespace using-declaration has no standalone declaration to render, but it
// likewise makes its introduced name available to the paper and marks its
// target for exposition-use rewriting in build_expos_set above.
class PaperEntityCollector : public clang::RecursiveASTVisitor<PaperEntityCollector> {
  public:
    PaperEntityCollector(const clang::SourceManager& sm, std::set<std::string>& out) : sm_(sm), out_(out) {}

    bool VisitNamedDecl(clang::NamedDecl* decl) {
        if (decl == nullptr || decl->isImplicit())
            return true;
        const beman::specgen::lowering::ItemDirectives directives = docblock_directives(decl, sm_);
        if (directives.elsewhere || (directives.expos && llvm::isa<clang::UsingDecl>(decl)))
            if (const std::string name = decl->getNameAsString(); !name.empty())
                out_.insert(name);
        return true;
    }

  private:
    const clang::SourceManager& sm_;
    std::set<std::string>&      out_;
};

std::optional<std::vector<std::string>> collect_paper_entities(const std::vector<clang::Decl*>& decls,
                                                               const clang::SourceManager&      sm) {
    std::set<std::string> declared;
    PaperEntityCollector  collector(sm, declared);
    // substrate generic algorithm: a for_each-shaped RecursiveASTVisitor walk.
    for (clang::Decl* decl : decls)
        collector.TraverseDecl(decl);
    std::vector<std::string> entities(declared.begin(), declared.end());
    if (entities.empty())
        return std::nullopt;
    return entities;
}

// The namespace path enclosing `decl`, most-enclosing first, joined with
// "::" ("" if `decl` is not lexically inside any named namespace). Same
// unfold-then-join shape as qualifier_namespace_name above (this file's
// second "::"-join call site; reuses its exact
// `std::views::join_with(std::string_view("::"))` expression rather than a
// third spelling -- a bare `"::"` literal would drag its NUL terminator into
// the join).
std::string namespace_path(const clang::Decl* decl) {
    std::vector<std::string> parts;
    // substrate generic algorithm: DeclContext parent-chain walk -- an
    // unfold over a linked structure, not a fold over a range that already
    // exists, same shape as qualifier_namespace_name's NestedNameSpecifier
    // walk (step 07).
    for (const clang::DeclContext* ctx = decl->getDeclContext(); ctx != nullptr; ctx = ctx->getParent())
        if (const auto* ns = llvm::dyn_cast<clang::NamespaceDecl>(ctx); ns != nullptr && !ns->isAnonymousNamespace())
            parts.push_back(ns->getNameAsString());
    if (parts.empty())
        return {};
    std::reverse(parts.begin(), parts.end());
    return parts | std::views::join_with(std::string_view("::")) | std::ranges::to<std::string>();
}

// The namespaces whose qualifiers are dropped from rendered code (design
// §3.5): `std` — the draft writes library names unqualified, being inside
// namespace std — plus every namespace the header itself declares its top-level
// entities in (`demo`, `beman::optional`), which maps onto `std` in the draft
// and so is likewise implicit. Everything else (notably `detail::`) is left
// verbatim for the leakage checker.
//
// "Top-level" is prefix-minimal, and has to be. collect_top_level_decl
// descends through nested namespaces, so the raw paths include `demo::detail`
// — the implementation namespace, whose qualifier this set exists to *keep*.
// Keeping that path would let the two spellings of one qualifier disagree:
// qualifier_namespace_name yields the chain as *written* (`detail`), which
// never equals the fully-qualified path (`demo::detail`), so `detail::storage`
// would stay while `demo::detail::storage` lost its qualifier entirely,
// hiding the leak instead of leaving it visible. Dropping every path that has
// another in the set as a proper prefix makes both spellings render the same
// way, because the prefix walk does the rest: an unmatched `demo::detail`
// chain is descended into, its `demo` prefix matches, and what is left is
// `detail::storage` either way. The same narrowing is what lets a real
// `std::ranges::` qualifier render as the draft writes it, `ranges::`.
std::set<std::string> build_namespace_drop_set(const std::vector<clang::Decl*>& decls) {
    std::set<std::string> paths = decls | std::views::transform(namespace_path) |
                                  std::views::filter([](const std::string& path) { return !path.empty(); }) |
                                  std::ranges::to<std::set<std::string>>();
    paths.insert("std");

    const auto nested_in_another = [&paths](const std::string& path) {
        return std::ranges::any_of(paths, [&path](const std::string& other) {
            return other.size() < path.size() && path.starts_with(other) && path[other.size()] == ':';
        });
    };
    return paths | std::views::filter(std::not_fn(nested_in_another)) | std::ranges::to<std::set<std::string>>();
}

// The complement of QualifierDropper's walk. That visitor answers "does
// this qualifier resolve into the drop set?" and, when it does, deletes it;
// the *no* answers are what design §9's leakage checker needs, and this
// visitor is what keeps them. A qualifier the drop set does not cover is left
// verbatim in rendered code, and a reader of the wording cannot see what
// `detail::` names — so the resolution the dropper already performs is
// exactly the resolution the check needs.
//
// Recorded per *namespace*, not per occurrence: the IR channel is
// document-level (ir::ForeignNamespace) and the validator locates occurrences
// by matching the name against rendered text, the same way its roster half
// locates a leaked member. That makes over-collection harmless — a qualifier
// written only inside a body the tool never renders contributes a name that
// matches nothing — which is why this walks every main-file decl rather than
// threading a second output through the six places QualifierDropper runs.
//
// Two guards keep the finding one-directional (a miss, never a false one):
//
//   - a namespace already in the drop set is skipped even when it is part of a
//     surviving chain, so `demo::detail::storage` reports `detail` alone;
//   - a namespace rooted in `std` is skipped. A `std::ranges::` qualifier is
//     the case this exists for: it survives the drop set (the set holds
//     `std`, not `std::ranges`) and belongs in the draft verbatim, because it
//     is the standard's own vocabulary. That is a property of the namespace's
//     qualified path, not of where it happens to be declared — the guard used
//     to be "declared in the main file", which let the identical rendered
//     `detail::` pass unreported the moment the helper namespace moved into
//     an included implementation header, disabling the leakage guarantee by
//     exactly the refactor the tool's own guidance encourages.
class ForeignQualifierCollector : public clang::RecursiveASTVisitor<ForeignQualifierCollector> {
  public:
    ForeignQualifierCollector(const std::set<std::string>& drop, std::map<std::string, std::string>& out)
        : drop_(drop), out_(out) {}

    bool TraverseNestedNameSpecifierLoc(clang::NestedNameSpecifierLoc nns) {
        if (nns) {
            const std::optional<std::string> written = qualifier_namespace_name(nns.getNestedNameSpecifier());
            if (written && drop_.count(*written) == 0)
                record(nns.getNestedNameSpecifier());
        }
        return clang::RecursiveASTVisitor<ForeignQualifierCollector>::TraverseNestedNameSpecifierLoc(nns);
    }

    bool TraverseDeclRefExpr(clang::DeclRefExpr* expr) {
        if (expr != nullptr && imported_qualifier_is_droppable(expr->getDecl(), expr->getQualifierLoc(), drop_))
            return true;
        return clang::RecursiveASTVisitor<ForeignQualifierCollector>::TraverseDeclRefExpr(expr);
    }

    bool TraverseConceptSpecializationExpr(clang::ConceptSpecializationExpr* expr) {
        if (expr != nullptr &&
            imported_qualifier_is_droppable(expr->getNamedConcept(), expr->getNestedNameSpecifierLoc(), drop_))
            return true;
        return clang::RecursiveASTVisitor<ForeignQualifierCollector>::TraverseConceptSpecializationExpr(expr);
    }

    bool TraverseUnresolvedLookupExpr(clang::UnresolvedLookupExpr* expr) {
        if (expr != nullptr && expr->getQualifierLoc() && expr->decls_begin() != expr->decls_end() &&
            std::ranges::all_of(expr->decls(), [&](clang::NamedDecl* decl) {
                return imported_qualifier_is_droppable(decl, expr->getQualifierLoc(), drop_);
            }))
            return true;
        return clang::RecursiveASTVisitor<ForeignQualifierCollector>::TraverseUnresolvedLookupExpr(expr);
    }

  private:
    // Each namespace the surviving qualifier writes, innermost first — the
    // same unfold qualifier_namespace_name makes, kept separate because that
    // one joins the chain into the single string the drop set is keyed on and
    // this one needs each link's decl.
    void record(clang::NestedNameSpecifier qualifier) {
        // substrate generic algorithm: the same NestedNameSpecifier unfold
        // qualifier_namespace_name performs (this file, step 07) -- each
        // qualifier yields only its own prefix, so the chain does not exist
        // as a range until the walk produces it.
        while (qualifier.getKind() == clang::NestedNameSpecifier::Kind::Namespace) {
            const clang::NamespaceAndPrefix np    = qualifier.getAsNamespaceAndPrefix();
            const auto*                     named = llvm::dyn_cast_or_null<clang::NamedDecl>(np.Namespace);
            if (named == nullptr)
                return;
            const std::string name      = named->getNameAsString();
            const std::string qualified = named->getQualifiedNameAsString();
            if (!name.empty() && drop_.count(qualified) == 0 && !qualified.starts_with("std::"))
                out_.emplace(name, qualified);
            qualifier = np.Prefix;
        }
    }

    const std::set<std::string>&        drop_;
    std::map<std::string, std::string>& out_;
};

std::vector<beman::specgen::ir::ForeignNamespace> collect_foreign_namespaces(const std::vector<clang::Decl*>& decls,
                                                                             const std::set<std::string>&     drop) {
    // The map deduplicates and orders by name, so the IR channel is stable
    // whichever decl wrote the qualifier first.
    std::map<std::string, std::string> found;
    ForeignQualifierCollector          collector(drop, found);
    // substrate generic algorithm: a for_each-shaped walk driving
    // RecursiveASTVisitor's side effect (appending to `found`), the same
    // shape build_document's collect_top_level_decl loop has -- not a
    // transform building a container from a range.
    for (clang::Decl* decl : decls)
        collector.TraverseDecl(decl);
    return found | std::views::transform([](const auto& kv) {
               return beman::specgen::ir::ForeignNamespace{kv.first, kv.second};
           }) |
           std::ranges::to<std::vector>();
}

// `ForeignQualifierCollector`'s complement for a name with no qualifier at
// all (issue #84): a helper sharing the header's own namespace, so nothing
// written at the use site ever names a foreign namespace, needs a check keyed
// on where the *referenced declaration* lives rather than on what the use
// site wrote. Same AST-node coverage `ExposUseFinder` reads a name from
// (`DeclRefExpr`, a concept reference, an unresolved lookup's candidates, and
// the three exposition-only type-use shapes) -- reused here for the opposite
// question: not "is this decl `\expos`-marked", but "is this decl invisible
// to the reader for a reason `\expos` was never asked to fix".
//
// Two guards keep this one-directional the same way `ForeignQualifierCollector`
// is:
//
//   - a decl already in `expos_` is skipped: it renders under its
//     `\exposid` sentinel, which is design §9's own escape hatch, and the
//     fix for a name this check *would* otherwise flag is exactly to mark it
//     `\expos` where it is actually declared -- `\expos` already reaches an
//     included, non-system header (issue #36), so no separate "trust me"
//     marker is needed;
//   - a decl in a system header is skipped: nothing under one carries specgen
//     markup, and a system header is the standard's own vocabulary, the same
//     boundary `collect_expos_scope_decl` already draws.
//
// A decl `DocumentFiles::contains` (this run's main file, or one of its
// gathered `.syn` follows, issue #77) is not "foreign" by construction: it is
// either documented through the ordinary path or reported by some other §9
// check (undocumented, `\omit`ted, private) -- this check is only about a
// declaration this run never had a chance to say anything about at all.
class ForeignDeclCollector : public clang::RecursiveASTVisitor<ForeignDeclCollector> {
  public:
    ForeignDeclCollector(const std::map<const clang::Decl*, std::string>& expos,
                         const clang::SourceManager&                      sm,
                         const DocumentFiles&                             doc,
                         std::map<std::string, std::string>&              out)
        : expos_(expos), sm_(sm), doc_(doc), out_(out) {}

    bool VisitDeclRefExpr(clang::DeclRefExpr* expr) {
        if (expr != nullptr)
            record(expr->getDecl());
        return true;
    }

    bool VisitConceptReference(clang::ConceptReference* ref) {
        if (ref != nullptr)
            record(ref->getNamedConcept());
        return true;
    }

    bool VisitUnresolvedLookupExpr(clang::UnresolvedLookupExpr* expr) {
        if (expr != nullptr)
            for (clang::NamedDecl* candidate : expr->decls()) // substrate generic algorithm
                record(candidate);
        return true;
    }

    bool VisitTypedefTypeLoc(clang::TypedefTypeLoc tl) {
        record(tl.getDecl());
        return true;
    }

    bool VisitTemplateSpecializationTypeLoc(clang::TemplateSpecializationTypeLoc tl) {
        record(tl.getTypePtr()->getTemplateName().getAsTemplateDecl());
        return true;
    }

    bool VisitTagTypeLoc(clang::TagTypeLoc tl) {
        record(tl.getDecl());
        return true;
    }

    // The shadow, as ExposUseFinder looks it up: one that is not exposition-
    // only still names its target, and the target is what may be foreign.
    bool VisitUsingTypeLoc(clang::UsingTypeLoc tl) {
        const clang::UsingShadowDecl* shadow = tl.getDecl();
        if (shadow != nullptr && !expos_.contains(shadow->getCanonicalDecl()))
            record(shadow->getTargetDecl());
        return true;
    }

  private:
    void record(const clang::NamedDecl* decl) {
        if (decl == nullptr)
            return;
        const clang::Decl* canonical = decl->getCanonicalDecl();
        if (const auto* specialization = llvm::dyn_cast<clang::VarTemplateSpecializationDecl>(decl))
            canonical = specialization->getSpecializedTemplate()->getCanonicalDecl();
        if (expos_.contains(canonical))
            return;
        // An enumerator is named through its enumeration: one of an
        // exposition-only enumeration is as visible as the enumeration is,
        // wherever that enumeration happens to be declared.
        if (const auto* enumerator = llvm::dyn_cast<clang::EnumConstantDecl>(decl);
            enumerator != nullptr &&
            expos_.contains(llvm::cast<clang::Decl>(enumerator->getDeclContext())->getCanonicalDecl()))
            return;
        const clang::SourceLocation loc = decl->getLocation();
        if (!loc.isValid() || sm_.isInSystemHeader(loc) || doc_.contains(sm_.getDecomposedLoc(loc).first))
            return;
        const std::string name = decl->getNameAsString();
        // Bare filename, not `diagnostic_file_name`'s path-relative-to-the-
        // main-file convention: that convention assumes the file it names
        // moves in lockstep with the main file (true for a `.syn` follow,
        // which is fixed relative to it by the very `#include` that pulled it
        // in), and this decl's declaring header is under no such constraint
        // -- reached by an arbitrary `-I` or quoted-relative search, so a
        // main file relocated on its own (as the east-const golden's flipped
        // copy is) would report a different relative path for the identical
        // fact.
        if (!name.empty())
            out_.emplace(name, llvm::sys::path::filename(sm_.getFilename(loc)).str());
    }

    const std::map<const clang::Decl*, std::string>& expos_;
    const clang::SourceManager&                      sm_;
    const DocumentFiles&                             doc_;
    std::map<std::string, std::string>&              out_;
};

// Every name the document itself declares, at any depth (decision
// shared-spelling-foreign-name). The check `ForeignDeclCollector` feeds
// *resolves* a reference but *reports* it by text, and a spelling can name
// two entities: a generated `detail::tables::windows_1252` and the
// `codec::windows_1252` enumerator it is the table for are the same word and
// different declarations, deliberately, because both are named after the
// encoding. Text-matching the foreign one then fires at every occurrence of
// the documented one -- starting with the enumeration's own declaration
// (issue #93), where the check reports an enumerator as undocumented at the
// point where the enumeration that documents it is being rendered.
//
// So this is deliberately wider than the roster's `documented` set the
// validator already consults, which is what let the report through: a roster
// records the enumeration, never its enumerators, so the run genuinely did
// not know it declared that spelling. It is wider at the other end too -- a
// private or `\omit`ted member's name suppresses the foreign report as well.
// That is the direction to be wrong in. A name this run declares is this
// run's to explain, and whether it explains it well is a question every
// other design §9 check is *already* asking about that same declaration, by
// disposition rather than by text; a foreign entity sharing its spelling is
// evidence of nothing, and reporting it costs the author an `\expos` that
// asserts something false about a header the document never reaches.
//
// Locations are checked, not decl contexts: a declaration written in the
// document is the document's whatever its scope -- a class member, an
// enumerator, a template parameter, a local of a body that never becomes
// wording.
class DocumentNameCollector : public clang::RecursiveASTVisitor<DocumentNameCollector> {
  public:
    DocumentNameCollector(const clang::SourceManager& sm, const DocumentFiles& doc, std::set<std::string>& out)
        : sm_(sm), doc_(doc), out_(out) {}

    bool VisitNamedDecl(clang::NamedDecl* decl) {
        // Implicit declarations are compiler synthesis, not source, the same
        // exclusion collect_top_level_decl draws: an implicit member reports
        // its class's own location, so it would contribute a name the author
        // never wrote at a place the author never wrote it.
        if (decl == nullptr || decl->isImplicit())
            return true;
        const clang::SourceLocation loc = decl->getLocation();
        if (!loc.isValid() || !doc_.contains(sm_.getDecomposedLoc(loc).first))
            return true;
        if (const std::string name = decl->getNameAsString(); !name.empty())
            out_.insert(name);
        return true;
    }

  private:
    const clang::SourceManager& sm_;
    const DocumentFiles&        doc_;
    std::set<std::string>&      out_;
};

std::vector<beman::specgen::ir::ForeignDeclaration>
collect_foreign_declarations(const std::vector<clang::Decl*>&                 decls,
                             const std::map<const clang::Decl*, std::string>& expos,
                             const clang::SourceManager&                      sm,
                             const DocumentFiles&                             doc) {
    std::map<std::string, std::string> found;
    ForeignDeclCollector               collector(expos, sm, doc, found);
    for (clang::Decl* decl : decls) // substrate generic algorithm
        collector.TraverseDecl(decl);

    // The same walk over the same declarations, asking the other question:
    // which spellings are the document's own. Two passes rather than one
    // visitor answering both, because the two sets are keyed on opposite
    // ends of a reference -- where a *referenced* declaration lives, and
    // where a *written* declaration lives -- and only their names ever meet.
    std::set<std::string> declared;
    DocumentNameCollector names(sm, doc, declared);
    for (clang::Decl* decl : decls) // substrate generic algorithm
        names.TraverseDecl(decl);

    return found | std::views::filter([&declared](const auto& kv) { return !declared.contains(kv.first); }) |
           std::views::transform(
               [](const auto& kv) { return beman::specgen::ir::ForeignDeclaration{kv.first, kv.second}; }) |
           std::ranges::to<std::vector>();
}

// --- what a non-extracted body names (design §9) ----------------------------
//
// Design §9 gives its leakage checker two severities, and the note one is
// about code the tool never prints: an undocumented helper that "appears only
// in non-extracted bodies". Every other validation rule reads something the IR
// already carries or something the front end can add beside it; this one
// cannot, and not by omission — a body with no `\effects-equiv` /
// `\returns-expr` marker is *by construction* the code that never becomes
// wording, so no node holds it and no amount of Tier-A reading finds it. What
// the front end records here is exactly that absence: the class members such a
// body names, for a validator that then asks the roster whether the reader
// could have seen them.
//
// The members a function body refers to, by name. Resolved references only —
// a `MemberExpr` or a `DeclRefExpr` whose declaration's parent is a class
// declared in the main file — which is narrower than scanning the body's text
// two ways that both matter. It excludes a local, a parameter and a `std::`
// name, none of which a roster ever holds; and it excludes a *dependent*
// member access (`this->helper()` through a dependent base), whose name
// resolves to no declaration here. The first exclusion is what keeps the
// Tier-A rule from reporting a body-local `tmp` that happens to share a name
// with a hidden member; the second is a miss, and is the direction every
// leakage rule errs in (a miss costs a finding, never invents one).
class BodyMemberCollector : public clang::RecursiveASTVisitor<BodyMemberCollector> {
  public:
    // `in_document` says which class a member must belong to for its name to
    // count: a class of the header's parse is in its main file, while a
    // source file's parse holds the classes in the files it includes (issue
    // #176).
    BodyMemberCollector(std::function<bool(const clang::CXXRecordDecl*)> in_document, std::set<std::string>& out)
        : in_document_(std::move(in_document)), out_(out) {}

    bool VisitMemberExpr(clang::MemberExpr* expr) {
        record(expr->getMemberDecl());
        return true;
    }

    // A static member function or static data member, and an unqualified call
    // to a member of the current instantiation that Sema resolved without an
    // implicit object argument, arrive as a plain reference rather than a
    // member access.
    bool VisitDeclRefExpr(clang::DeclRefExpr* expr) {
        record(expr->getDecl());
        return true;
    }

  private:
    void record(const clang::NamedDecl* decl) {
        if (decl == nullptr)
            return;
        const auto* parent = llvm::dyn_cast_or_null<clang::CXXRecordDecl>(decl->getDeclContext());
        if (parent == nullptr || !in_document_(parent))
            return;
        const std::string name = decl->getNameAsString();
        if (!name.empty())
            out_.insert(name);
    }

    std::function<bool(const clang::CXXRecordDecl*)> in_document_;
    std::set<std::string>&                           out_;
};

// Does `fn` carry wording whose body the tool leaves unrendered? Three
// conditions, each excluding a body whose contents are somebody else's
// question:
//
//   - it has a docblock: an undocumented function's body specifies nothing,
//     so what it reaches for is an implementation detail all the way down;
//   - the docblock does not say `\omit`/`\merge`: the same, one step later —
//     the entity is declared but deliberately unspecified;
//   - the docblock carries no extraction marker: an extracted body *is*
//     wording, so a hidden name in it is design §9's **error** case, which
//     the leakage checker reports off the rendered `EquivalentTo` text.
bool has_unextracted_body(const clang::FunctionDecl* fn, const clang::SourceManager& sm) {
    if (fn == nullptr || !fn->isThisDeclarationADefinition() || fn->getBody() == nullptr)
        return false;
    const clang::RawComment* rc = attached_raw_comment(fn);
    if (rc == nullptr)
        return false;
    const llvm::StringRef raw = rc->getRawText(sm);
    if (!is_docblock_comment(raw))
        return false;

    const beman::specgen::lowering::Lowered lowered =
        beman::specgen::lowering::lower(beman::specgen::grammar::parse_docblock(raw.str()).block);
    if (suppresses_generated_entity(lowered.directives))
        return false;
    return std::ranges::none_of(lowered.descr.elements, [](const beman::specgen::ir::DescriptionElement& element) {
        return element.equivalent.has_value();
    });
}

// How a finding names the body it is about: `<class>::<name>` for a member,
// the bare name for a free function. Not a qualified name — the enclosing
// namespace maps onto `std` in the draft (design §3.5), so printing it would
// put a namespace in a diagnostic that the rendered wording deliberately does
// not have. This is the same "which class is this about" answer
// `ir::Synopsis::name` gives, spelled for a member instead of a type.
std::string body_owner_label(const clang::FunctionDecl* fn) {
    const clang::CXXRecordDecl* record = enclosing_record(fn);
    if (record == nullptr)
        return fn->getNameAsString();
    // A constructor's own name is the *injected class name*, which for a
    // class template carries the parameter list -- `optional<T>` -- and would
    // print as `optional::optional<T>`. The class already stands to the left
    // of the `::`, so the plain record name is both shorter and what a reader
    // writing the header saw.
    const std::string name =
        llvm::isa<clang::CXXConstructorDecl>(fn) ? record->getNameAsString() : fn->getNameAsString();
    return record->getNameAsString() + "::" + name;
}

// Every (documented function with an unrendered body, member it names) pair in
// the header. Both halves of the same two-part walk every marker pre-pass
// makes — out-of-line definitions from `decls`, in-class definitions and
// hidden friends from each record's members — since a body is a body wherever
// it is written.
// The members one function's body names, paired with the label that names the
// body. Pairs rather than BodyUses so a set can order them without a
// comparator; the two are the same two strings.
std::vector<std::pair<std::string, std::string>>
body_member_uses(const clang::FunctionDecl* fn, std::function<bool(const clang::CXXRecordDecl*)> in_document) {
    std::set<std::string> names;
    BodyMemberCollector   collector(std::move(in_document), names);
    collector.TraverseStmt(fn->getBody());
    const std::string owner = body_owner_label(fn);
    return names | std::views::transform([owner](const std::string& member) { return std::pair{owner, member}; }) |
           std::ranges::to<std::vector>();
}

std::vector<beman::specgen::ir::BodyUse> collect_unextracted_uses(const std::vector<clang::Decl*>& decls,
                                                                  const clang::SourceManager&      sm) {
    const auto uses_of = [&sm](const clang::FunctionDecl* fn) {
        return body_member_uses(
            fn, [&sm](const clang::CXXRecordDecl* parent) { return sm.isInMainFile(parent->getLocation()); });
    };

    const auto bodies = [&sm](const clang::FunctionDecl* fn) { return has_unextracted_body(fn, sm); };

    // Out-of-line function definitions.
    std::vector<const clang::FunctionDecl*> functions = decls | std::views::transform(as_out_of_line_function) |
                                                        std::views::filter(bodies) |
                                                        std::ranges::to<std::vector<const clang::FunctionDecl*>>();

    // In-class definitions and hidden friends, the shape collect_inclass_items
    // attaches wording from.
    functions.append_range(decls | std::views::transform(as_record_decl) |
                           std::views::filter([](const clang::CXXRecordDecl* record) {
                               return record != nullptr && record->isThisDeclarationADefinition();
                           }) |
                           std::views::transform([](const clang::CXXRecordDecl* record) {
                               return record->decls() | std::views::transform(member_function_or_template) |
                                      std::ranges::to<std::vector<const clang::FunctionDecl*>>();
                           }) |
                           std::views::join | std::views::filter(bodies));

    // The set deduplicates and orders, so the IR channel does not depend on
    // which decl reached a member first — two constructors sharing an owner
    // label and a helper contribute one entry, which is also the finding a
    // reader wants (this class's wording leans on that helper).
    const std::set<std::pair<std::string, std::string>> found =
        functions | std::views::transform(uses_of) | std::views::join |
        std::ranges::to<std::set<std::pair<std::string, std::string>>>();

    return found | std::views::transform([](const auto& pair) {
               return beman::specgen::ir::BodyUse{pair.first, pair.second};
           }) |
           std::ranges::to<std::vector>();
}

// The function operands the synopsis/itemdecl renders as `\seebelow`
// (design §4.3): canonical decls map to the selected operand.
// Same pre-pass
// shape as build_omit_set (out-of-line defs keyed to getFirstDecl(), in-class
// members to themselves), so extract_synopsis has the set when a class is seen.
SeeBelowMap build_seebelow_map(const std::vector<clang::Decl*>& decls, const clang::SourceManager& sm) {
    const auto marked_target = [&sm](const clang::FunctionDecl* fn) {
        return seebelow_target(docblock_directives(fn, sm));
    };

    // Out-of-line function definitions, keyed to their in-class declaration.
    SeeBelowMap result =
        decls | std::views::transform(as_out_of_line_function) | std::views::filter([](const clang::FunctionDecl* fn) {
            return fn != nullptr && fn->isThisDeclarationADefinition() && fn->isOutOfLine();
        }) |
        std::views::filter([&](const clang::FunctionDecl* fn) { return marked_target(fn).has_value(); }) |
        std::views::transform([&](const clang::FunctionDecl* fn) {
            return std::pair<const clang::Decl*, SeeBelowTarget>{fn->getFirstDecl()->getCanonicalDecl(),
                                                                 *marked_target(fn)};
        }) |
        std::ranges::to<SeeBelowMap>();

    // In-class members of a class or class template: member_as_function_or_friend's
    // FunctionDecl-or-FriendDecl unwrap, plus (unlike build_omit_set) a plain
    // in-class function template -- \seebelow can mark one, and unlike a
    // friend target this is not ambiguous with anything build_omit_set already
    // recognizes, so it is a local addition rather than a change to the shared
    // projection.
    const auto seebelow_members_of = [&marked_target](const clang::CXXRecordDecl* record) {
        const std::vector<const clang::Decl*> members(record->decls_begin(), record->decls_end());
        return members | std::views::transform([](const clang::Decl* member) -> const clang::FunctionDecl* {
                   if (const clang::FunctionDecl* fn = member_as_function_or_friend(member))
                       return fn;
                   if (const auto* ft = llvm::dyn_cast<clang::FunctionTemplateDecl>(member))
                       return ft->getTemplatedDecl();
                   return nullptr;
               }) |
               std::views::filter([](const clang::FunctionDecl* fn) { return fn != nullptr; }) |
               std::views::filter([&](const clang::FunctionDecl* fn) { return marked_target(fn).has_value(); }) |
               std::views::transform([&](const clang::FunctionDecl* fn) {
                   return std::pair<const clang::Decl*, SeeBelowTarget>{fn->getCanonicalDecl(), *marked_target(fn)};
               }) |
               std::ranges::to<std::vector<std::pair<const clang::Decl*, SeeBelowTarget>>>();
    };
    result.insert_range(decls | std::views::transform(as_record_decl) |
                        std::views::filter([](const clang::CXXRecordDecl* record) {
                            return record != nullptr && record->isThisDeclarationADefinition();
                        }) |
                        std::views::transform(seebelow_members_of) | std::views::join);

    return result;
}

// The literal draft synopsis suffix requested by \freestanding or
// \freestanding-deleted. Like SeeBelowMap, this is transient front-end
// metadata keyed to the canonical in-class declaration: an out-of-line
// definition commonly carries the markup, but the earlier class synopsis is
// where the comment must be emitted.
FreestandingMap build_freestanding_map(const std::vector<clang::Decl*>& decls, const clang::SourceManager& sm) {
    const auto marked_kind = [&sm](const clang::FunctionDecl* fn) -> std::optional<FreestandingKind> {
        const beman::specgen::lowering::ItemDirectives dirs = docblock_directives(fn, sm);
        if (dirs.freestanding_deleted)
            return FreestandingKind::Deleted;
        if (dirs.freestanding)
            return FreestandingKind::Freestanding;
        return std::nullopt;
    };

    FreestandingMap result =
        decls | std::views::transform(as_out_of_line_function) | std::views::filter([](const clang::FunctionDecl* fn) {
            return fn != nullptr && fn->isThisDeclarationADefinition() && fn->isOutOfLine();
        }) |
        std::views::filter([&](const clang::FunctionDecl* fn) { return marked_kind(fn).has_value(); }) |
        std::views::transform([&](const clang::FunctionDecl* fn) {
            return std::pair<const clang::Decl*, FreestandingKind>{fn->getFirstDecl()->getCanonicalDecl(),
                                                                   *marked_kind(fn)};
        }) |
        std::ranges::to<FreestandingMap>();

    const auto marked_members_of = [&marked_kind](const clang::CXXRecordDecl* record) {
        const std::vector<const clang::Decl*> members(record->decls_begin(), record->decls_end());
        return members | std::views::transform(member_function_or_template) |
               std::views::filter([](const clang::FunctionDecl* fn) { return fn != nullptr; }) |
               std::views::filter([&](const clang::FunctionDecl* fn) { return marked_kind(fn).has_value(); }) |
               std::views::transform([&](const clang::FunctionDecl* fn) {
                   return std::pair<const clang::Decl*, FreestandingKind>{fn->getCanonicalDecl(), *marked_kind(fn)};
               }) |
               std::ranges::to<std::vector<std::pair<const clang::Decl*, FreestandingKind>>>();
    };
    result.insert_range(decls | std::views::transform(as_record_decl) |
                        std::views::filter([](const clang::CXXRecordDecl* record) {
                            return record != nullptr && record->isThisDeclarationADefinition();
                        }) |
                        std::views::transform(marked_members_of) | std::views::join);
    return result;
}

// --- stage 1: classify() ----------------------------------------------------
//
// classify() converts one RawItem into a document_build::DocEvent (decision
// document-build-stages' closed, clang-free variant): the dyn_cast if/else
// chain over clang::Decl
// subclasses is inherently Tier B and is quarantined here — this is the only
// function downstream of RawItem collection that still touches a
// clang::Decl*. build_document() calls it once per RawItem and hands the
// resulting events to document_build::build_tree, clang-free from here on
// (see include/beman/specgen/document_build.hpp, including its top-of-file
// note on why \also/empty-descr grouping is decided partly here and partly
// inside build_tree rather than as a later pass over the finished tree).
namespace db = beman::specgen::document_build;

struct UnrecognizedSectionHeader {
    std::string stable_name;
    unsigned    line_offset = 0;
};

std::optional<UnrecognizedSectionHeader> unrecognized_section_header(std::string_view raw);

// Fold one attached item into its DocEvent: classify()'s shared tail for
// every top-level decl that becomes an ItemDecl — an out-of-line or free
// function definition, or a documented record declaration the header never
// defines. Kept out of classify() so both arms make the same
// `\omit`/`\merge` and grouping decisions rather than drifting apart.
beman::specgen::document_build::DocEvent item_decl_event(AttachedItem&& attached, const DocumentFiles& doc) {
    namespace db = beman::specgen::document_build;

    if (suppresses_generated_entity(attached.directives))
        // \omit / \merge (design §4.3): no itemdescr. Removal from the
        // synopsis is handled by the omit-set pre-pass (build_omit_set +
        // extract_synopsis), which ran before this decl was reached. The
        // docblock's own findings still travel: the entity is
        // unspecified deliberately, but a malformed marker in the block that
        // says so is not deliberate.
        return db::Ignored{std::move(attached.diagnostics)};

    // This is only the "does this item *want* to join" half of \also/
    // empty-descr grouping — a property of this item alone. Whether a join
    // actually happens depends on tree-adjacency context this function does
    // not have (is there a preceding primary in the same open \rSec frame,
    // in push order); that half belongs to document_build::build_tree, which
    // evaluates it before this item's descr is touched by anything, so a
    // follower that turns out to have no primary keeps its own content
    // (design §4.3; see document_build.hpp's top-of-file note for the two
    // review findings this split fixes: a follower's content must not be
    // discarded just because it *asked* to join, and the join must be
    // decided in push order, not the placement-key-sorted order the tree
    // ends up in).
    const bool named_grouping = attached.directives.group_id || attached.directives.also_target;
    const bool wants_join     = !named_grouping && (attached.directives.also || attached.item.descr.elements.empty());

    return db::ItemDecl{doc.offset_in(attached.file, attached.inclass_offset),
                        wants_join,
                        std::move(attached.item),
                        std::move(attached.diagnostics),
                        std::move(attached.directives.group_id),
                        std::move(attached.directives.also_target),
                        attached.grouping_line};
}

// A record declaration that is not a definition. Three cases, none of which
// is the empty Synopsis node this used to produce (whose rendering was an
// empty code block — worse than nothing in a paper):
//
//  - The entity is defined elsewhere: the definition's own event carries the
//    synopsis, and this redeclaration is plumbing. No node, silent.
//  - Documented and never defined: an undefined class-template primary (or
//    plain record) whose declaration *is* the wording — an ordinary itemdecl
//    plus the docblock's description, through item_decl_event so
//    `\omit`/`\merge` and `\also` grouping mean what they mean everywhere
//    else.
//  - Undocumented and never defined: omitted, silent — the same treatment
//    every unmarked entity gets (design §6).
beman::specgen::document_build::DocEvent
classify_record_declaration(const clang::NamedDecl*                          decl,
                            const clang::CXXRecordDecl*                      record,
                            const clang::SourceManager&                      sm,
                            const DocumentFiles&                             doc,
                            const clang::LangOptions&                        lang_opts,
                            const std::set<std::string>&                     ns_drop_set,
                            const std::map<const clang::Decl*, std::string>& expos_set) {
    namespace db = beman::specgen::document_build;

    if (record == nullptr || record->hasDefinition())
        return db::Ignored{};
    if (!has_docblock(decl, sm))
        return db::Ignored{};
    return item_decl_event(attach_record_declaration(decl, record, sm, lang_opts, ns_drop_set, expos_set), doc);
}

// The main-file byte ranges of raw comments some processed declaration
// consumed as its own docblock (top-level decls and class members alike).
// classify() consults this for a comment carrying a verbatim marker: attached,
// the block belongs to its declaration's event, and the comment's own RawItem
// must not become a second node (issue #4).
using AttachedCommentRanges = std::vector<std::pair<unsigned, unsigned>>;

bool is_attached_comment(unsigned offset, const AttachedCommentRanges& ranges) {
    return std::ranges::any_of(
        ranges, [offset](const auto& range) { return offset >= range.first && offset <= range.second; });
}

// The function `decl` declares at namespace scope, when it is a declaration
// without a body: the shape whose definition may be in a source file (issue
// #175). A member, a hidden friend and a definition are none of them it.
const clang::FunctionDecl* declared_free_function(const clang::Decl* decl) {
    const clang::FunctionDecl* fn = llvm::dyn_cast_or_null<clang::FunctionDecl>(decl);
    if (fn == nullptr) {
        if (const auto* tmpl = llvm::dyn_cast_or_null<clang::FunctionTemplateDecl>(decl))
            fn = tmpl->getTemplatedDecl();
    }
    if (fn == nullptr || llvm::isa<clang::CXXMethodDecl>(fn) || fn->isThisDeclarationADefinition() ||
        !fn->getDeclContext()->isFileContext() || !fn->getLexicalDeclContext()->isFileContext())
        return nullptr;
    return fn;
}

db::DocEvent classify(const RawItem&                                   ev,
                      const clang::SourceManager&                      sm,
                      const DocumentFiles&                             doc,
                      const clang::LangOptions&                        lang_opts,
                      const std::set<const clang::Decl*>&              omit_set,
                      const std::map<const clang::Decl*, std::string>& expos_set,
                      const SeeBelowMap&                               seebelow_map,
                      const FreestandingMap&                           freestanding_map,
                      const std::set<std::string>&                     ns_drop_set,
                      const SkippedRanges&                             skipped,
                      const AttachedCommentRanges&                     attached_comments,
                      const SourceMarkupMap&                           source_markup) {
    namespace ir = beman::specgen::ir;

    if (ev.decl == nullptr) {
        if (ev.comment_text.find("\\verbatim-synopsis") != std::string::npos ||
            ev.comment_text.find("\\verbatim-itemdecl") != std::string::npos) {
            const std::optional<std::size_t> start = docblock_start(ev.comment_text);
            if (start) {
                const std::string_view markup = std::string_view(ev.comment_text).substr(*start);
                grammar::ParseResult   parsed = grammar::parse_docblock(markup);
                // Attached to a declaration — and not `\omit`/`\merge`d away
                // there — the block is that declaration's own docblock: the
                // declaration's event substitutes the authored text and
                // reports the block's findings, and a second, standalone
                // node here was the duplication (issue #4). An
                // `\omit`/`\merge`d declaration contributes no node of its
                // own, so for that pairing the standalone node still speaks,
                // exactly as it does for a detached block whose entity is
                // never declared (spec_verbatim_record_merge.hpp).
                if ((parsed.block.verbatim_itemdecl || parsed.block.markers.verbatim_synopsis) &&
                    is_attached_comment(ev.offset, attached_comments) &&
                    !suppresses_generated_entity(parsed.block.markers))
                    return db::Ignored{};
                const unsigned first_line = document_line(sm, doc, ev.offset) +
                                            static_cast<unsigned>(std::ranges::count(
                                                std::string_view(ev.comment_text).substr(0, *start), '\n'));
                std::vector<db::Diagnostic> diagnostics =
                    parsed.diags | std::views::transform([first_line](const grammar::Diagnostic& diagnostic) {
                        const unsigned line =
                            diagnostic.line > 0 ? first_line + static_cast<unsigned>(diagnostic.line) - 1 : first_line;
                        return db::Diagnostic{diagnostic.severity, line, diagnostic.message};
                    }) |
                    std::ranges::to<std::vector<db::Diagnostic>>();

                if (parsed.block.verbatim_itemdecl) {
                    lowering::Lowered lowered = lowering::lower(parsed.block);
                    ir::SpecItem      item;
                    item.decl.signatures.push_back(ir::CodeText{std::move(*parsed.block.verbatim_itemdecl), {}});
                    item.descr = std::move(lowered.descr);
                    return db::ItemDecl{ev.offset, false, std::move(item), std::move(diagnostics)};
                }
                if (parsed.block.markers.verbatim_synopsis) {
                    db::SynopsisDecl out;
                    out.offset        = ev.offset;
                    out.synopsis.code = ir::CodeText{parsed.block.verbatim_synopsis.value_or(""), {}};
                    out.exact         = true;
                    out.diagnostics   = std::move(diagnostics);
                    return out;
                }
            }
        }

        if (const auto end = section_end_fence(ev.comment_text);
            end && !std::string_view(end->stable).ends_with(".syn")) {
            const unsigned fence_offset = ev.offset + static_cast<unsigned>(end->line_begin);
            const auto [file, local]    = doc.locate(fence_offset);
            return db::SectionClose{end->stable, sm.getLineNumber(file, local), diagnostic_file_name(sm, file)};
        }

        const parse::parse_result<SectionHeader> header = parse_rsec(ev.comment_text);
        if (!header) {
            if (rsec_tag_recognized(ev.comment_text)) {
                // The \rSec tag itself matched; the failure is in the
                // depth/[stable]/{title} grammar after it -- a malformed
                // \rSec. The comment still contributes no node (design
                // §3.2), but the fact is reported rather than silently
                // dropped. Two positions
                // are worth having: Diagnostic::offset locates the comment in
                // the file (what the driver prints as the location), while
                // the parse failure's own offset locates the bad character
                // inside it.
                const parse::parse_error& failure = header.error();
                // The comment's own line in the main file: RawItem carries a
                // byte offset (its place in the interleave), and the line is
                // what the driver prints.
                return db::Ignored{{db::Diagnostic{beman::specgen::Severity::Warning,
                                                   document_line(sm, doc, ev.offset),
                                                   std::format("malformed \\rSec marker: {} (comment offset {})",
                                                               failure.message,
                                                               failure.where.offset)}}};
            }
            if (const auto candidate = unrecognized_section_header(ev.comment_text)) {
                return db::Ignored{
                    {db::Diagnostic{beman::specgen::Severity::Warning,
                                    document_line(sm, doc, ev.offset) + candidate->line_offset,
                                    std::format("unrecognized section header [{}]; use \\rSec<depth>[{}]{{title}}",
                                                candidate->stable_name,
                                                candidate->stable_name)}}};
            }
            return db::Ignored{}; // \ref group headers, license/SPDX text, or
                                  // trailing braces (design §3.2): none of
                                  // these are structure, and none are errors.
        }
        return db::SectionOpen{ev.offset, header->value.depth, header->value.stable, header->value.title};
    }

    // A marked namespace-scope concept or variable is a
    // complete free-standing synopsis. The source spelling remains its IR
    // name; only the rendered declaration identifier takes the exposid name.
    //
    // A *defined* class template, or a specialization of one, is the
    // exception: it takes the ordinary class arms below, where extract_synopsis
    // gives it its exposid name and `// exposition only` tail. The standalone
    // arm copies the declaration's text through its semicolon, which for a
    // class definition kept every member body, every `\omit`ted member, and
    // every docblock and Doxygen comment in the class body, and collected none
    // of its members' own wording.
    const clang::NamedDecl* namespace_expos = nullptr;
    if (is_namespace_expos_candidate(ev.decl) && !is_defined_record(ev.decl))
        namespace_expos = llvm::cast<clang::NamedDecl>(ev.decl);
    if (namespace_expos != nullptr && expos_set.contains(namespace_expos->getCanonicalDecl())) {
        // The marker that brought it here is not the only one its docblock may
        // carry: bare `\seebelow` on a variable composes with `\expos` (issue
        // #38), and this arm used to read neither that nor the findings the
        // docblock's own parse produced.
        AttachedItem marked;
        attach_docblock(marked, ev.decl, sm);
        const VariableMask mask = variable_seebelow_mask(ev.decl, marked.directives, marked.grouping_line);
        if (mask.diagnostic)
            marked.diagnostics.push_back(*mask.diagnostic);
        // The kinds whose *definition* is the implementation mask it the same
        // way (issue #50): an alias's RHS, an alias template's, a concept's
        // constraint-expression. Bare `\seebelow` only -- the targeted forms
        // have no meaning here either, and variable_seebelow_mask has already
        // said so for the kinds it speaks for.
        std::optional<clang::SourceRange> see_below;
        if (marked.directives.seebelow && !marked.directives.seebelow_target)
            see_below = definition_mask_range(ev.decl, sm, lang_opts);

        db::SynopsisDecl out;
        out.offset        = ev.offset;
        out.synopsis.name = namespace_expos->getNameAsString();
        out.synopsis.code = extract_namespace_expos_synopsis(
            namespace_expos, sm, lang_opts, ns_drop_set, expos_set, mask.unspecified, see_below);
        // Its description renders beside that synopsis, the way a class's own
        // description does (design §4.3): the draft describes exposition-only
        // concepts and aliases in prose -- "the exposition-only concept ... is
        // satisfied when". The docblock was parsed for the directives above and
        // its description then discarded, which dropped that wording silently.
        out.descr       = std::move(marked.item.descr);
        out.diagnostics = std::move(marked.diagnostics);
        return out;
    }

    // A documented namespace-scope alias (template), variable (template), or
    // concept is an ordinary wording item (design §6): the declaration
    // itself is the itemdecl and the docblock's description follows, the
    // way [concept.same] and [tuple.helper] write them. Marked `\expos` it
    // took the standalone-synopsis arm above instead (every marked kind here
    // is an expos candidate); undocumented it stays absent, like every other
    // unannotated entity. Aliases route through attach_alias, whose masking
    // rules (`\seebelow`/`\impdef`) and grouping apply at namespace scope
    // exactly as they do in a class body.
    if (const clang::NamedDecl* entity = as_namespace_entity(ev.decl);
        entity != nullptr && has_docblock(ev.decl, sm)) {
        if (const auto* alias_tmpl = llvm::dyn_cast<clang::TypeAliasTemplateDecl>(entity))
            return item_decl_event(
                attach_alias(alias_tmpl->getTemplatedDecl(), sm, lang_opts, ns_drop_set, expos_set, alias_tmpl), doc);
        if (const auto* alias = llvm::dyn_cast<clang::TypeAliasDecl>(entity))
            return item_decl_event(attach_alias(alias, sm, lang_opts, ns_drop_set, expos_set), doc);
        return item_decl_event(attach_namespace_entity(entity, sm, lang_opts, ns_drop_set, expos_set), doc);
    }

    // A class/struct/union definition heads a synopsis; every other top-level
    // decl (free function, out-of-line member definition, variable, ...) is a
    // spec item. A defined class gets its synopsis text extracted (design
    // §3.4); a declaration that defines nothing goes through
    // classify_record_declaration — an itemdecl when it is a documented
    // undefined primary, no node at all otherwise, and never an empty
    // Synopsis (which rendered as an empty code block).
    //
    // A class template is not itself a CXXRecordDecl — top-level iteration
    // yields the ClassTemplateDecl, whose templated decl (getTemplatedDecl())
    // is the CXXRecordDecl carrying the actual members — so it needs its own
    // arm or it would fall through to SpecItem and lose the whole class.
    // extract_synopsis takes the ClassTemplateDecl as the extraction range's
    // head so the synopsis starts at `template`, not at `class`.
    if (const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(ev.decl)) {
        if (!record->isThisDeclarationADefinition())
            return classify_record_declaration(record, record, sm, doc, lang_opts, ns_drop_set, expos_set);
        if (auto diagnostics = record_suppression_diagnostics(record, sm))
            return db::Ignored{std::move(*diagnostics)};
        // An attached \verbatim-synopsis replaces the extracted synopsis text
        // (design §4.3, issue #4) — authored, span-free — while members,
        // roster, and derived class wording are collected exactly as for an
        // extracted one. The rest of the same docblock is the class's own
        // description, attached below.
        RecordDocblock   block = record_docblock(record, sm);
        db::SynopsisDecl out;
        out.offset        = ev.offset;
        out.synopsis.name = record->getNameAsString();
        out.diagnostics   = std::move(block.diagnostics);
        out.synopsis.code =
            block.synopsis
                ? ir::CodeText{std::move(*block.synopsis), {}}
                : extract_synopsis(
                      record, sm, lang_opts, omit_set, expos_set, seebelow_map, freestanding_map, ns_drop_set);
        collect_inclass_items(
            record, sm, lang_opts, ns_drop_set, expos_set, skipped, source_markup, out.pending, out.diagnostics);
        // After collection: the roster reads the routed sections
        // collect_inclass_items just decided.
        // Build the roster before grouping so every alias offset still
        // identifies its own routed declaration, including an \also follower.
        out.synopsis.roster = build_roster(record, sm, expos_set, source_markup, out.pending);
        group_adjacent_aliases(out.pending);
        attach_class_description(out, std::move(block), record, sm, lang_opts, ns_drop_set, expos_set);
        // After the description exists, and after grouping, which has no
        // opinion about it: `\at` on the class's own docblock sends both
        // halves of that wording to the section it names (issue #98).
        route_class_description(out, record, docblock_directives(ev.decl, sm));
        return out;
    }
    if (const auto* tmpl = llvm::dyn_cast<clang::ClassTemplateDecl>(ev.decl)) {
        const clang::CXXRecordDecl* templated = tmpl->getTemplatedDecl();
        if (templated == nullptr || !templated->isThisDeclarationADefinition())
            return classify_record_declaration(tmpl, templated, sm, doc, lang_opts, ns_drop_set, expos_set);
        if (auto diagnostics = record_suppression_diagnostics(tmpl, sm))
            return db::Ignored{std::move(*diagnostics)};
        // Same attached-\verbatim-synopsis substitution, and the same class
        // description, as the plain-record arm above; the docblock is looked
        // up on the ClassTemplateDecl, the decl the arm's other docblock
        // reads use.
        RecordDocblock   block = record_docblock(tmpl, sm);
        db::SynopsisDecl out;
        out.offset        = ev.offset;
        out.synopsis.name = templated->getNameAsString();
        out.diagnostics   = std::move(block.diagnostics);
        out.synopsis.code = block.synopsis ? ir::CodeText{std::move(*block.synopsis), {}}
                                           : extract_synopsis(templated,
                                                              sm,
                                                              lang_opts,
                                                              omit_set,
                                                              expos_set,
                                                              seebelow_map,
                                                              freestanding_map,
                                                              ns_drop_set,
                                                              tmpl);
        collect_inclass_items(
            templated, sm, lang_opts, ns_drop_set, expos_set, skipped, source_markup, out.pending, out.diagnostics);
        // Build the roster before grouping so every alias offset still
        // identifies its own routed declaration, including an \also follower.
        out.synopsis.roster = build_roster(templated, sm, expos_set, source_markup, out.pending);
        group_adjacent_aliases(out.pending);
        attach_class_description(out, std::move(block), templated, sm, lang_opts, ns_drop_set, expos_set);
        // Same `\at` routing as the plain-record arm above, read from the
        // ClassTemplateDecl the arm's other docblock reads use.
        route_class_description(out, templated, docblock_directives(ev.decl, sm));
        return out;
    }

    // \also/\omit (design §4.3, Model A — see build_spec_item): an omitted
    // function definition contributes no node at all; a described overload
    // starts a group and an \also/empty-descr follower joins its signature
    // onto the group's primary instead of starting a new SpecItem. Only
    // function-definition items participate — a non-function decl (empty
    // SpecItem, is_function_def == false) can neither join nor act as a group
    // primary.
    // A namespace-scope function whose definition is in a source file (issue
    // #175): its item was built in that file's parse and sits here, at the
    // position of its declaration in this document, as if defined here.
    if (const clang::FunctionDecl* declared = declared_free_function(ev.decl)) {
        if (const auto sourced = source_markup.find(declared->getCanonicalDecl()); sourced != source_markup.end()) {
            AttachedItem attached   = sourced->second->attached;
            attached.inclass_offset = sm.getDecomposedLoc(ev.decl->getBeginLoc()).second;
            attached.file           = sm.getFileID(ev.decl->getBeginLoc());
            if (suppresses_generated_entity(attached.directives))
                return db::Ignored{std::move(attached.diagnostics)};
            return item_decl_event(std::move(attached), doc);
        }
    }

    AttachedItem attached = build_spec_item(ev.decl, sm, lang_opts, ns_drop_set, expos_set, skipped);
    if (!attached.is_function_def) {
        // A top-level decl no arm above turned into a node. Undocumented,
        // that is the ordinary fate of an unannotated entity (design §6),
        // and it stays silent. *Documented*, it means authored wording is
        // about to be dropped, and dropping it silently is the failure mode
        // a vocabulary header suffers worst — the header validates clean
        // while its descriptions vanish. An `\omit`/`\merge`/`\expos` marker
        // is the author asking for no itemdescr; anything else is an Error
        // naming the kind, so an unsupported entity kind is loud the first
        // time someone documents one.
        if (has_docblock(ev.decl, sm)) {
            const beman::specgen::lowering::ItemDirectives dirs = docblock_directives(ev.decl, sm);
            if (!suppresses_generated_entity(dirs) && !dirs.expos) {
                const unsigned line = document_line(sm, doc, ev.offset);
                // A documented function *declaration* is the one shape here
                // with a better answer than "unsupported": the markup
                // belongs at the definition, which is what places a
                // function's wording (design §3.3). A deduction guide is a
                // FunctionDecl that can never have one, so it stays with the
                // unsupported-kind report.
                const clang::FunctionDecl* fn = as_out_of_line_function(ev.decl);
                std::string message = fn != nullptr && !llvm::isa<clang::CXXDeductionGuideDecl>(fn)
                                          ? "a documented function declaration produces no wording; markup belongs at "
                                            "the definition, which places a function's wording (design §3.3)"
                                          : std::format("a documented {} produces no wording: unsupported entity kind",
                                                        ev.decl->getDeclKindName());
                return db::Ignored{{db::Diagnostic{beman::specgen::Severity::Error,
                                                   line,
                                                   std::move(message),
                                                   diagnostic_file_name(sm, sm.getFileID(ev.decl->getBeginLoc()))}}};
            }
        }
        return db::Ignored{};
    }
    return item_decl_event(std::move(attached), doc);
}

// Recognize the other section-heading convention real headers use,
// `// 22.5.3.3 Destructor[optional.dtor]`. It cannot open a section because it
// carries no depth, but silently treating it as ordinary prose files every
// following declaration into the previous section.
//
// This is intentionally a warning heuristic, not a second section grammar.
// Requiring numbered heading text and a final multi-component dotted name
// avoids ordinary prose references (`[structure.specifications]`), synopsis
// group headers (`\ref{...}`), trailing references, and Doxygen's
// `/// END [optional.syn]`. RawComment merges consecutive `//` lines, so scan
// each physical line and retain its offset for the diagnostic location.
std::optional<UnrecognizedSectionHeader> unrecognized_section_header(std::string_view raw) {
    const auto is_name_char = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
               c == '+';
    };
    const auto is_dotted_name = [&](std::string_view name) {
        return name.contains('.') && name.front() != '.' && name.back() != '.' && !name.contains("..") &&
               std::ranges::all_of(name, [&](char c) { return c == '.' || is_name_char(c); });
    };

    std::size_t line_begin  = 0;
    unsigned    line_offset = 0;
    // substrate generic algorithm: scanning preserves both the byte position
    // in the merged RawComment and the physical-line offset used by the
    // diagnostic; views::split would discard the first and require a second
    // stateful pass to recover the latter.
    while (line_begin <= raw.size()) {
        const std::size_t newline  = raw.find('\n', line_begin);
        const std::size_t line_end = newline == std::string_view::npos ? raw.size() : newline;
        std::string_view  line     = raw.substr(line_begin, line_end - line_begin);

        const std::size_t first = line.find_first_not_of(" \t");
        if (first != std::string_view::npos) {
            line.remove_prefix(first);
            if (line_vocabulary(llvm::StringRef{line}) == CommentVocabulary::Draft && line.starts_with("//")) {
                line.remove_prefix(2);
                const std::size_t body_begin = line.find_first_not_of(" \t");
                if (body_begin != std::string_view::npos) {
                    line.remove_prefix(body_begin);
                    const std::size_t body_end = line.find_last_not_of(" \t\r");
                    line                       = line.substr(0, body_end + 1);
                    const std::size_t open     = line.rfind('[');
                    if (open != std::string_view::npos && line.ends_with(']') && line.front() >= '0' &&
                        line.front() <= '9') {
                        const std::string_view stable = line.substr(open + 1, line.size() - open - 2);
                        if (is_dotted_name(stable))
                            return UnrecognizedSectionHeader{std::string(stable), line_offset};
                    }
                }
            }
        }

        if (newline == std::string_view::npos)
            break;
        line_begin = newline + 1;
        ++line_offset;
    }
    return std::nullopt;
}

// Clang coalesces adjacent line comments into one RawComment, but each
// physical \rSec or END line is a distinct structure event. Split only around
// those lines: the intervening chunks stay intact so multi-line docblocks and
// other comment consumers retain their existing input shape. Offsets remain
// file offsets rather than offsets into the merged RawComment.
void append_structure_comment_items(std::vector<RawItem>& out, unsigned raw_begin, std::string raw) {
    const std::size_t first = raw.find_first_not_of(" \t");
    if (first == std::string::npos || !std::string_view(raw).substr(first).starts_with("//")) {
        out.push_back(RawItem{raw_begin, nullptr, std::move(raw)});
        return;
    }

    std::size_t chunk_begin = 0;
    std::size_t line_begin  = 0;
    // substrate generic algorithm: a source-position-preserving partition at
    // structural lines; views::split would discard the byte offsets needed to
    // put the resulting events and diagnostics at their physical locations.
    while (line_begin < raw.size()) {
        const std::size_t      newline  = raw.find('\n', line_begin);
        const std::size_t      line_end = newline == std::string::npos ? raw.size() : newline;
        const std::string_view line(raw.data() + line_begin, line_end - line_begin);
        if (rsec_tag_recognized(line) || section_end_fence(line)) {
            if (chunk_begin < line_begin) {
                out.push_back(RawItem{raw_begin + static_cast<unsigned>(chunk_begin),
                                      nullptr,
                                      raw.substr(chunk_begin, line_begin - chunk_begin)});
            }

            std::size_t after_line = newline == std::string::npos ? raw.size() : newline + 1;
            // A formatter-wrapped `{title}` continues on the following plain
            // `//` lines (issue #8): while the brace is still open, extend
            // this item through each adjacent draft-form continuation — never
            // a `///`/`//!` line or another `\rSec` — so parse_rsec sees the
            // whole marker. A title the RawComment never closes stays
            // unterminated and draws the malformed-\rSec warning as before.
            const auto title_open = [&raw, line_begin](std::size_t end) {
                const std::string_view text(raw.data() + line_begin, end - line_begin);
                const std::size_t      open = text.find('{');
                return open != std::string_view::npos && text.find('}', open) == std::string_view::npos;
            };
            // substrate generic algorithm: the same offset-preserving line
            // scan as the outer loop, consuming continuations until the
            // title closes or the run of plain `//` lines ends.
            while (after_line < raw.size() && title_open(after_line)) {
                const std::size_t next_newline = raw.find('\n', after_line);
                const std::size_t next_end     = next_newline == std::string::npos ? raw.size() : next_newline;
                std::string_view  next(raw.data() + after_line, next_end - after_line);
                const std::size_t lead = next.find_first_not_of(" \t");
                if (lead == std::string_view::npos)
                    break;
                next.remove_prefix(lead);
                if (!next.starts_with("//") || next.starts_with("///") || next.starts_with("//!") ||
                    rsec_tag_recognized(next))
                    break;
                after_line = next_newline == std::string::npos ? raw.size() : next_newline + 1;
            }
            out.push_back(RawItem{raw_begin + static_cast<unsigned>(line_begin),
                                  nullptr,
                                  raw.substr(line_begin, after_line - line_begin)});
            chunk_begin = after_line;
            line_begin  = after_line;
            continue;
        }

        if (newline == std::string::npos)
            break;
        line_begin = newline + 1;
    }

    if (chunk_begin < raw.size())
        out.push_back(RawItem{raw_begin + static_cast<unsigned>(chunk_begin), nullptr, raw.substr(chunk_begin)});
}

// The draft writes every synopsis at namespace scope inside the namespace
// it belongs to -- `namespace std {`, its body indented two spaces, then the
// closing brace -- and the namespace mapping (design §3.5) has already made
// that namespace `std` by dropping the implementation's own. Each span moves
// with the character it starts on; an end moves with the last character it
// covers, so the indentation added at the start of the next line stays
// outside it.
void enclose_in_namespace_std(beman::specgen::ir::CodeText& code) {
    if (code.text.empty())
        return;
    std::string              text = "namespace std {\n";
    std::vector<std::size_t> moved(code.text.size());
    bool                     line_start = true;
    // substrate generic algorithm: one pass building the indented text and
    // the old-to-new offset map the spans are rewritten through.
    for (std::size_t k = 0; k < code.text.size(); ++k) {
        const char c = code.text[k];
        if (line_start && c != '\n')
            text.append(kNamespaceIndent, ' ');
        moved[k] = text.size();
        text.push_back(c);
        line_start = c == '\n';
    }
    if (!text.ends_with('\n'))
        text.push_back('\n');
    text.push_back('}');
    std::ranges::for_each(code.spans, [&moved](ir::Span& span) {
        const std::size_t begin = span.begin;
        span.begin              = moved[begin];
        span.end                = span.end > begin ? moved[span.end - 1] + 1 : span.begin;
    });
    code.text = std::move(text);
}

void append_synopsis_code(beman::specgen::ir::CodeText& out, beman::specgen::ir::CodeText part) {
    if (part.text.empty())
        return;
    if (!out.text.empty() && !out.text.ends_with("\n\n")) {
        if (!out.text.ends_with('\n'))
            out.text.push_back('\n');
        out.text.push_back('\n');
    }
    const std::size_t base = out.text.size();
    out.text += part.text;
    out.spans.append_range(part.spans | std::views::transform([base](ir::Span span) {
                               span.begin += base;
                               span.end += base;
                               return span;
                           }));
}

// A declaration that ordinary classification intentionally ignores still
// belongs in a bounded header synopsis. Extract it through its semicolon,
// applying the same qualifier, exposition-use, and index sentinel machinery
// as the established class and namespace-exposition paths.
beman::specgen::ir::CodeText extract_header_declaration(clang::Decl*                                     decl,
                                                        const clang::SourceManager&                      sm,
                                                        const clang::LangOptions&                        lang_opts,
                                                        const std::set<std::string>&                     ns_drop_set,
                                                        const std::map<const clang::Decl*, std::string>& expos_set,
                                                        const clang::VarDecl*             unspecified = nullptr,
                                                        std::optional<clang::SourceRange> see_below   = std::nullopt) {
    const unsigned        decl_begin = sm.getDecomposedLoc(decl->getBeginLoc()).second;
    clang::SourceLocation end_loc    = clang::Lexer::getLocForEndOfToken(decl->getEndLoc(), 0, sm, lang_opts);
    if (const std::optional<clang::Token> semi = clang::Lexer::findNextToken(decl->getEndLoc(), sm, lang_opts);
        semi && semi->is(clang::tok::semi))
        end_loc = clang::Lexer::getLocForEndOfToken(semi->getLocation(), 0, sm, lang_opts);
    const unsigned decl_end = sm.getDecomposedLoc(end_loc).second;

    const llvm::StringRef           buffer = file_buffer(sm, decl->getBeginLoc());
    std::string                     text   = buffer.substr(decl_begin, decl_end - decl_begin).str();
    std::vector<SynopsisEdit>       edits;
    std::map<std::string, SpanInfo> sentinels;
    unsigned                        span_n = 0;

    // A body is never synopsis content (design §3.4).  A region that follows
    // its includes (issue #77) holds function *definitions* -- the header the
    // definitions are written in is the header being specified -- so the
    // splice extract_synopsis does inside a class body is needed here too.
    // `= default` and `= delete` are kept, as they are there.
    //
    // Nothing is edited inside the spliced range, per §3.4: the
    // descending-offset apply loop would take an inner edit first and its
    // overlap watermark would then suppress the splice, which is how a body
    // that named a droppable qualifier survived into the synopsis while a body
    // that named nothing spliced cleanly.
    std::optional<std::pair<unsigned, unsigned>> body_splice;
    if (const clang::FunctionDecl* fn = function_or_template(decl);
        fn != nullptr && fn->doesThisDeclarationHaveABody() && !fn->isDefaulted() && !fn->isDeleted())
        if (const clang::Stmt* body = fn->getBody(); body != nullptr && body->getSourceRange().isValid()) {
            const unsigned body_begin = sm.getDecomposedLoc(body->getBeginLoc()).second;
            if (body_begin > decl_begin && body_begin < decl_end) {
                body_splice = std::pair{body_begin, decl_end};
                edits.push_back(SynopsisEdit{body_begin, decl_end, ";"});
            }
        }
    const auto outside_body = [&](unsigned begin, unsigned end) {
        return !body_splice || end <= body_splice->first || begin >= body_splice->second;
    };

    edits.append_range(
        namespace_qualifier_edits(decl, ns_drop_set, sm, lang_opts, expos_set) |
        std::views::filter([&](const auto& range) {
            return range.first >= decl_begin && range.second <= decl_end && outside_body(range.first, range.second);
        }) |
        std::views::transform([](const auto& range) { return SynopsisEdit{range.first, range.second, ""}; }));
    // substrate generic algorithm: a conditional edit fold over resolved
    // exposition uses; each accepted use can contribute two source edits.
    for (const ExposUse& use : expos_uses(decl, expos_set, sm, lang_opts)) {
        if (use.name_begin < decl_begin || use.name_end > decl_end)
            continue;
        if (!outside_body(use.name_begin, use.name_end))
            continue;
        const std::string sentinel = span_sentinel(span_n++);
        sentinels[sentinel]        = SpanInfo{ir::SpanKind::ExposId, use.display, use.display};
        edits.push_back(SynopsisEdit{use.name_begin, use.name_end, sentinel});
        if (use.qualifier_end > use.qualifier_begin)
            edits.push_back(SynopsisEdit{use.qualifier_begin, use.qualifier_end, ""});
    }

    add_declaration_masks(
        edits, sentinels, span_n, unspecified, see_below, buffer, decl_begin, decl_end, sm, lang_opts);

    const auto*                named    = llvm::dyn_cast<clang::NamedDecl>(decl);
    const clang::FunctionDecl* named_fn = named != nullptr ? llvm::dyn_cast<clang::FunctionDecl>(named) : nullptr;
    if (named != nullptr)
        if (const auto* tmpl = llvm::dyn_cast<clang::FunctionTemplateDecl>(named))
            named_fn = tmpl->getTemplatedDecl();
    // A deduction guide's AST name (`<deduction guide for X>`) is not its
    // source spelling, so an index sentinel would swap the guide's class-name
    // token for that angle-bracketed label (issue #22). A guide renders
    // verbatim and unindexed, as the draft's header synopses show them.
    const bool is_guide = named_fn != nullptr && llvm::isa<clang::CXXDeductionGuideDecl>(named_fn);
    if (named != nullptr && !is_guide && !named->getNameAsString().empty() && named->getLocation().isValid()) {
        clang::SourceRange name_range{named->getLocation(), named->getLocation()};
        if (named_fn != nullptr)
            name_range = named_fn->getNameInfo().getSourceRange();
        const unsigned name_begin = sm.getDecomposedLoc(name_range.getBegin()).second;
        const unsigned name_end =
            sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(name_range.getEnd(), 0, sm, lang_opts)).second;
        if (name_begin >= decl_begin && name_end <= decl_end &&
            !std::ranges::any_of(edits, [&](const SynopsisEdit& edit) {
                return edits_overlap(edit, SynopsisEdit{name_begin, name_end, {}});
            })) {
            const std::string sentinel = span_sentinel(span_n++);
            sentinels[sentinel]        = SpanInfo{ir::SpanKind::LibraryIndex, named->getNameAsString(), ""};
            edits.push_back(SynopsisEdit{name_begin, name_end, sentinel});
        }
    }

    sort_back_to_front(edits);
    unsigned applied_begin = decl_end + 1;
    // substrate generic algorithm: descending source-edit application with an
    // overlap watermark, the same operation as the established extractors.
    for (const SynopsisEdit& edit : edits) {
        if (edit.end > applied_begin)
            continue;
        text.replace(edit.begin - decl_begin, edit.end - edit.begin, edit.replacement);
        applied_begin = edit.begin;
    }
    return format_and_recover(std::move(text), sentinels, std::nullopt, kNamespaceIndent);
}

beman::specgen::ir::CodeText header_comment_code(std::string text) {
    if (text.empty())
        return {};
    std::map<std::string, SpanInfo> sentinels;
    unsigned                        span_n = 0;
    text                                   = apply_ref_sentinels(std::move(text), span_n, sentinels);
    return recover_sentinels(std::move(text), sentinels);
}

// --- source files (issue #149) ---------------------------------------------
//
// A member declared in the header and defined in a source file carries its
// markup on that definition, which the header's own parse never sees. Each
// source file named with --source is parsed on its own, with its own compile
// flags, and every documented out-of-line member definition in it is built
// into its item there, as attach_function builds one for a definition in the
// header. The source file includes the header, so the in-class declaration it
// defines is in that parse too, and the item is keyed by where that
// declaration is: its file and byte offset, which the header's parse reads
// identically. A definition that matches no declaration is a compile error in
// the source file, so a mistyped signature fails the parse rather than leaving
// a stray item.

// Where a declaration is, the same in any parse that reads its file: the
// file's canonical path, and the byte offset in it. `main_path` names the main
// file, which the parse holds as an in-memory buffer rather than a file it
// opened.
using DeclPosition = std::pair<std::string, unsigned>;

std::string canonical_file_path(const clang::SourceManager& sm, clang::FileID file, std::string_view main_path) {
    std::string path;
    if (file == sm.getMainFileID())
        path = std::string(main_path);
    else if (const clang::OptionalFileEntryRef entry = sm.getFileEntryRefForID(file))
        path = entry->getName().str();
    else
        return {};
    std::error_code ec;
    return std::filesystem::weakly_canonical(path, ec).generic_string();
}

DeclPosition decl_position(const clang::Decl* decl, const clang::SourceManager& sm, std::string_view main_path) {
    const auto [file, offset] = sm.getDecomposedLoc(decl->getBeginLoc());
    return {canonical_file_path(sm, file, main_path), offset};
}

// The FriendDecl that declares `fn` in its class, whose `friend` keyword the
// itemdecl keeps, as collect_inclass_items keeps it for a hidden friend.
clang::SourceLocation friend_keyword(const clang::FunctionDecl* fn) {
    const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(fn->getLexicalDeclContext());
    if (record == nullptr || fn->getFriendObjectKind() == clang::Decl::FOK_None)
        return {};
    const clang::Decl* declared = fn->getDescribedFunctionTemplate() != nullptr
                                      ? static_cast<const clang::Decl*>(fn->getDescribedFunctionTemplate())
                                      : fn;
    const auto         friends  = record->friends();
    const auto         found    = std::ranges::find_if(friends, [declared](const clang::FriendDecl* friend_decl) {
        return friend_decl->getFriendDecl() == declared;
    });
    return found != friends.end() ? (*found)->getBeginLoc() : clang::SourceLocation{};
}

// The declarations a parse holds in the files `wanted` accepts, descending
// through namespaces and linkage specifications.
template <class Wanted>
void collect_file_decls(clang::Decl*                decl,
                        const clang::SourceManager& sm,
                        const Wanted&               wanted,
                        std::vector<clang::Decl*>&  out) {
    if (llvm::isa<clang::NamespaceDecl, clang::LinkageSpecDecl>(decl)) {
        // substrate generic algorithm: recursive descent over a decl tree, the
        // same shape as collect_top_level_decl's.
        for (clang::Decl* child : llvm::cast<clang::DeclContext>(decl)->decls())
            collect_file_decls(child, sm, wanted, out);
        return;
    }
    if (decl->isImplicit() || !decl->getBeginLoc().isValid())
        return;
    if (wanted(sm.getFileID(sm.getExpansionLoc(decl->getBeginLoc()))))
        out.push_back(decl);
}

struct SourceHarvest {
    std::map<DeclPosition, SourceMarkup>                    items;
    std::vector<beman::specgen::document_build::Diagnostic> diagnostics;
    std::vector<std::string>                                sources;
    // The members named by the bodies of the definitions that became items
    // but render none of them (issue #176), as the header's own parse
    // collects them for a definition there.
    std::vector<std::pair<std::string, std::string>> unextracted_uses;
};

// Build the items for one source file's documented member definitions.
// `document_paths` are the canonical paths of the files the header's document
// is made of. Any other documented declaration in the source file produces no
// wording, and says so, rather than being dropped.
std::expected<SourceHarvest, BuildFailure> harvest_source_file(const SourceFile&            source,
                                                               const std::set<std::string>& document_paths) {
    namespace db = beman::specgen::document_build;

    ParsedHeader parsed = parse_header(source.path, source.options);
    if (!parsed.ast)
        return std::unexpected(unparsed_header(source.path));
    if (parsed.had_error)
        return std::unexpected(BuildFailure{std::format(
            "Clang reported an error parsing '{}'; the parse is incomplete, so the wording generated from it "
            "would be unreliable",
            source.path)});

    const clang::SourceManager& sm        = parsed.ast->getSourceManager();
    const clang::LangOptions&   lang_opts = parsed.ast->getLangOpts();
    const clang::FileID         main_file = sm.getMainFileID();

    std::map<clang::FileID, bool> in_document;
    const auto                    is_document_file = [&](clang::FileID file) {
        const auto [it, inserted] = in_document.try_emplace(file, false);
        if (inserted)
            it->second = document_paths.contains(canonical_file_path(sm, file, source.path));
        return it->second;
    };

    // The header's declarations as this parse sees them, for the same
    // pre-passes the header's parse makes, so an item built here spells its
    // itemdecl and rewrites its body as it would there.
    std::vector<clang::Decl*> header_decls;
    std::vector<clang::Decl*> source_decls;
    std::vector<clang::Decl*> expos_decls;
    // substrate generic algorithm: the same iterator-pair-turned-subrange,
    // for_each-shaped side effect as build_document_with_sources' walks.
    for (clang::Decl* decl : std::ranges::subrange(parsed.ast->top_level_begin(), parsed.ast->top_level_end())) {
        collect_file_decls(decl, sm, is_document_file, header_decls);
        collect_file_decls(decl, sm, [main_file](clang::FileID file) { return file == main_file; }, source_decls);
        collect_expos_scope_decl(decl, sm, main_file, expos_decls);
    }
    expos_decls.append_range(header_decls);

    const std::map<const clang::Decl*, std::string> expos_set   = build_expos_set(expos_decls, sm);
    const std::set<std::string>                     ns_drop_set = build_namespace_drop_set(header_decls);
    const SkippedRanges                             skipped     = collect_skipped_ranges(*parsed.ast);

    SourceHarvest harvest;
    harvest.sources    = parse_sources(source.path, *parsed.ast);
    const auto line_of = [&](const clang::Decl* decl) { return sm.getSpellingLineNumber(decl->getBeginLoc()); };
    const auto report  = [&](const clang::Decl* decl, std::string message) {
        harvest.diagnostics.push_back(
            db::Diagnostic{beman::specgen::Severity::Error, line_of(decl), std::move(message), source.path});
    };

    // substrate generic algorithm: a filter-map with three distinct
    // rejections, each a finding, over the source file's declarations.
    for (const clang::Decl* decl : source_decls) {
        if (!has_docblock(decl, sm))
            continue;
        const clang::FunctionDecl* fn    = as_out_of_line_function(decl);
        const std::string          named = llvm::isa<clang::NamedDecl>(decl)
                                               ? llvm::cast<clang::NamedDecl>(decl)->getQualifiedNameAsString()
                                               : std::string{"this declaration"};
        if (fn == nullptr || !fn->isThisDeclarationADefinition()) {
            report(decl,
                   "a documented declaration in a source file produces no wording: a source file supplies markup "
                   "only on the definition of a function declared in the header");
            continue;
        }
        const clang::FunctionDecl* first             = fn->getFirstDecl();
        const bool                 declared_in_class = llvm::isa<clang::CXXRecordDecl>(first->getLexicalDeclContext());
        const bool                 declared_free     = !declared_in_class && declared_free_function(first) != nullptr;
        if (first == fn || (!declared_in_class && !declared_free)) {
            report(decl,
                   std::format("the documented definition of `{}` produces no wording: a source file supplies "
                               "markup only for a function declared in the header, in a class body or at "
                               "namespace scope",
                               named));
            continue;
        }
        if (!is_document_file(sm.getFileID(first->getBeginLoc()))) {
            report(decl,
                   std::format("the documented definition of `{}` produces no wording: its class is declared in "
                               "'{}', which is not part of this document",
                               named,
                               canonical_file_path(sm, sm.getFileID(first->getBeginLoc()), source.path)));
            continue;
        }

        AttachedItem attached =
            attach_function(fn, first, friend_keyword(first), sm, lang_opts, ns_drop_set, expos_set, skipped);
        // A finding positioned in this parse's main file names no file, which
        // the driver reads as the header; name the source file instead.
        std::ranges::for_each(attached.diagnostics, [&](db::Diagnostic& diagnostic) {
            if (diagnostic.file.empty())
                diagnostic.file = source.path;
        });
        if (has_unextracted_body(fn, sm))
            harvest.unextracted_uses.append_range(body_member_uses(fn, [&](const clang::CXXRecordDecl* parent) {
                return is_document_file(sm.getFileID(parent->getLocation()));
            }));
        harvest.items.emplace(decl_position(first, sm, source.path),
                              SourceMarkup{std::move(attached), source.path, line_of(fn)});
    }
    return harvest;
}

// The source files' items, matched to the in-class declarations of the
// header's parse. Each class definition the document describes is walked,
// nested ones included, and an item that matches none of their members is
// reported: its class is in the document's files, but no class the document
// describes holds it.
SourceMarkupMap match_source_markup(const std::vector<clang::Decl*>&                         decls,
                                    const clang::SourceManager&                              sm,
                                    std::string_view                                         header_path,
                                    const std::map<DeclPosition, SourceMarkup>&              items,
                                    std::vector<beman::specgen::document_build::Diagnostic>& diagnostics) {
    SourceMarkupMap        matched;
    std::set<DeclPosition> used;
    const auto             match_members = [&](this const auto& self, const clang::CXXRecordDecl* record) -> void {
        // substrate generic algorithm: a recursive walk over class bodies that
        // fills two lookups at once.
        for (const clang::Decl* member : record->decls()) {
            if (const clang::CXXRecordDecl* nested = as_record_decl(member);
                nested != nullptr && nested->isThisDeclarationADefinition())
                self(nested);
            const clang::FunctionDecl* fn = member_function_or_template(member);
            if (fn == nullptr)
                continue;
            const DeclPosition position = decl_position(fn, sm, header_path);
            if (const auto found = items.find(position); found != items.end()) {
                matched.emplace(fn->getCanonicalDecl(), &found->second);
                used.insert(position);
            }
        }
    };
    std::ranges::for_each(decls | std::views::transform(as_record_decl) |
                              std::views::filter([](const clang::CXXRecordDecl* record) {
                                  return record != nullptr && record->isThisDeclarationADefinition();
                              }),
                          match_members);
    // A namespace-scope function declared in the document is matched by the
    // position of that declaration (issue #175).
    std::ranges::for_each(decls | std::views::transform(declared_free_function) |
                              std::views::filter([](const clang::FunctionDecl* fn) { return fn != nullptr; }),
                          [&](const clang::FunctionDecl* fn) {
                              const DeclPosition position = decl_position(fn, sm, header_path);
                              if (const auto found = items.find(position); found != items.end()) {
                                  matched.emplace(fn->getCanonicalDecl(), &found->second);
                                  used.insert(position);
                              }
                          });
    std::ranges::for_each(items | std::views::filter([&](const auto& item) { return !used.contains(item.first); }),
                          [&](const auto& item) {
                              diagnostics.push_back(beman::specgen::document_build::Diagnostic{
                                  beman::specgen::Severity::Error,
                                  item.second.line,
                                  "this documented definition produces no wording: it defines no member or function "
                                  "this document declares",
                                  item.second.file});
                          });
    return matched;
}

} // namespace

std::expected<db::BuildResult, BuildFailure> build_document(std::string_view    header_path,
                                                            const ParseOptions& options) {
    // A projection of the richer entry point, not a second parse and not a
    // second implementation (see DocumentBuild in frontend.hpp): the callers
    // that want only the document say so by calling this.
    return build_document_with_sources(header_path, options).transform([](DocumentBuild built) {
        return std::move(built.result);
    });
}

std::expected<DocumentBuild, BuildFailure> build_document_with_sources(std::string_view               header_path,
                                                                       const ParseOptions&            options,
                                                                       const std::vector<SourceFile>& source_files) {
    namespace ir = beman::specgen::ir;

    ParsedHeader parsed = parse_header(header_path, options);
    if (!parsed.ast)
        return std::unexpected(unparsed_header(header_path));
    // Distinct from the message above, and deliberately so — Clang did
    // build an AST here, but its own DiagnosticsEngine says the parse that
    // produced it hit an error, which buildASTFromCodeWithArgs's recovery can
    // paper over with a non-null ASTUnit (ParsedHeader's doc comment above has
    // the mechanism). Reported as a BuildFailure rather than swallowed: this
    // pipeline's extraction is lexical, so a header Clang could not fully
    // parse does not fail loudly with a missing type — it fails quietly with
    // a plausible-looking wrong one (an unlinked
    // out-of-line definition renders `gadget::`-qualified and the roster calls
    // it undocumented), and `generate`'s output is wording a reader will trust.
    if (parsed.had_error)
        return std::unexpected(BuildFailure{std::format(
            "Clang reported an error parsing '{}'; the parse is incomplete, so the wording generated from it "
            "would be unreliable",
            header_path)});

    clang::ASTContext&        ctx       = parsed.ast->getASTContext();
    clang::SourceManager&     sm        = parsed.ast->getSourceManager();
    const clang::FileID       main_file = sm.getMainFileID();
    const clang::LangOptions& lang_opts = parsed.ast->getLangOpts();

    // Which files this document is: the main file, and the ones it `#include`s
    // inside a gathered `.syn` region (issue #77).  Everything below orders and
    // compares *document* offsets, which are the same numbers as before for a
    // document that is one file.
    const DocumentFiles doc = DocumentFiles::discover(*parsed.ast, sm);

    std::vector<clang::Decl*> decls;
    // substrate generic algorithm: same iterator-pair-turned-subrange,
    // for_each-shaped side effect as collect_interleaved's identical walk
    // above.
    for (clang::Decl* decl : std::ranges::subrange(parsed.ast->top_level_begin(), parsed.ast->top_level_end()))
        collect_top_level_decl(decl, sm, doc, decls);

    // Pre-passes: members to drop from every synopsis
    // (`\omit`/`\merge`) and exposition-only members (`\expos`) to render with an
    // `\exposid` span — both needed before the first extract_synopsis call below.
    // `\expos` alone reads a wider set than the document-structure walk
    // collected: the marked declaration may live in an included header while
    // the signature naming it is here (issue #36).
    std::vector<clang::Decl*> expos_decls = decls;
    // substrate generic algorithm: the same iterator-pair-turned-subrange,
    // for_each-shaped side effect as the walk just above.
    for (clang::Decl* decl : std::ranges::subrange(parsed.ast->top_level_begin(), parsed.ast->top_level_end()))
        collect_expos_scope_decl(decl, sm, main_file, expos_decls);

    std::set<const clang::Decl*>                    omit_set         = build_omit_set(decls, sm);
    const std::map<const clang::Decl*, std::string> expos_set        = build_expos_set(expos_decls, sm);
    const std::optional<std::vector<std::string>>   paper_entities   = collect_paper_entities(decls, sm);
    SeeBelowMap                                     seebelow_map     = build_seebelow_map(decls, sm);
    FreestandingMap                                 freestanding_map = build_freestanding_map(decls, sm);
    const std::set<std::string>                     ns_drop_set      = build_namespace_drop_set(decls);
    const SkippedRanges                             skipped          = collect_skipped_ranges(*parsed.ast);

    // The source files' items (issue #149), each built in its own parse and
    // matched here to the declaration it defines. Their markers act on the
    // header's synopsis just as a definition's in the header do, so they join
    // the same pre-pass sets.
    std::set<std::string> document_paths{canonical_file_path(sm, doc.main(), header_path)};
    document_paths.insert_range(doc.parts() | std::views::transform([&](const DocumentFiles::Part& part) {
                                    return canonical_file_path(sm, part.file, header_path);
                                }));
    std::map<DeclPosition, SourceMarkup>                    source_items;
    std::vector<beman::specgen::document_build::Diagnostic> source_diagnostics;
    std::vector<std::string>                                source_reads;
    std::vector<std::pair<std::string, std::string>>        source_uses;
    // substrate generic algorithm: a short-circuiting fold over the source
    // files, each of which may fail the build, merging three results.
    for (const SourceFile& source : source_files) {
        std::expected<SourceHarvest, BuildFailure> harvest = harvest_source_file(source, document_paths);
        if (!harvest)
            return std::unexpected(std::move(harvest.error()));
        source_items.merge(harvest->items);
        source_uses.append_range(std::move(harvest->unextracted_uses));
        source_diagnostics.append_range(std::move(harvest->diagnostics));
        source_reads.append_range(std::move(harvest->sources));
    }
    const SourceMarkupMap source_markup =
        match_source_markup(decls, sm, header_path, source_items, source_diagnostics);
    std::ranges::for_each(source_markup, [&](const auto& entry) {
        const auto& [decl, markup] = entry;
        if (suppresses_generated_entity(markup->attached.directives))
            omit_set.insert(decl);
        if (const std::optional<SeeBelowTarget> target = seebelow_target(markup->attached.directives))
            seebelow_map.emplace(decl, *target);
        if (markup->attached.directives.freestanding_deleted)
            freestanding_map.emplace(decl, FreestandingKind::Deleted);
        else if (markup->attached.directives.freestanding)
            freestanding_map.emplace(decl, FreestandingKind::Freestanding);
    });

    // The docblock ranges some declaration consumed — top-level decls and
    // class members alike, since every comment in the file becomes a RawItem
    // below. classify() consults this for a comment carrying a verbatim
    // marker: attached, the declaration's own event substitutes the authored
    // text, and the comment's standalone node was the duplication (issue #4).
    AttachedCommentRanges attached_comments;
    const auto            note_attached_docblock = [&](const clang::Decl* decl) {
        const clang::RawComment* rc = attached_raw_comment(decl);
        if (rc == nullptr || !is_docblock_comment(rc->getRawText(sm)))
            return;
        const auto [begin_file, begin] = sm.getDecomposedLoc(rc->getBeginLoc());
        const auto [end_file, end]     = sm.getDecomposedLoc(rc->getEndLoc());
        if (begin_file == end_file && doc.contains(begin_file))
            attached_comments.emplace_back(doc.offset_in(begin_file, begin), doc.offset_in(end_file, end));
    };
    // substrate generic algorithm: a for_each-shaped walk driving
    // note_attached_docblock's side effect, over the same decls-plus-members
    // shape the build_omit_set pre-pass walks.
    for (const clang::Decl* decl : decls) {
        note_attached_docblock(decl);
        if (const clang::CXXRecordDecl* record = as_record_decl(decl);
            record != nullptr && record->isThisDeclarationADefinition())
            // substrate generic algorithm: the same side-effecting walk, one
            // level down — a class's members feed the same collector.
            for (const RealRecordMember& member : real_record_members(record))
                note_attached_docblock(member.decl);
    }

    std::vector<RawItem> raw_items;
    raw_items.reserve(decls.size());
    raw_items.append_range(decls | std::views::transform([&](clang::Decl* decl) {
                               return RawItem{doc.offset(sm, decl->getBeginLoc()), decl, {}};
                           }));
    const auto collect_comments = [&](clang::FileID file) {
        if (const std::map<unsigned, clang::RawComment*>* comments = ctx.Comments.getCommentsInFile(file)) {
            // substrate generic algorithm: one RawComment expands to one or
            // more RawItems, so this is a flat-map into the existing event
            // buffer.
            for (const auto& [offset, comment] : *comments)
                append_structure_comment_items(raw_items, doc.offset_in(file, offset), comment->getRawText(sm).str());
        }
    };
    collect_comments(doc.main());
    // substrate generic algorithm: a for_each-shaped walk driving
    // collect_comments' side effect over the followed files.
    for (const DocumentFiles::Part& part : doc.parts())
        collect_comments(part.file);
    // Same ordering contract as collect_interleaved (design §3.2).
    std::stable_sort(
        raw_items.begin(), raw_items.end(), [](const RawItem& a, const RawItem& b) { return a.offset < b.offset; });

    // The qualifiers ns_drop_set did *not* cover, collected before the
    // pipeline goes clang-free below and attached to the finished document.
    // It is document-level data, so it rides past build_tree rather than
    // through it — no DocEvent carries it and no node owns it.
    const std::vector<ir::ForeignNamespace> foreign = collect_foreign_namespaces(decls, ns_drop_set);

    // The bare-name complement (issue #84), collected the same way and for
    // the same reason: a name whose declaration this run never documents,
    // found by where the reference actually resolves rather than by what the
    // use site wrote.
    const std::vector<ir::ForeignDeclaration> foreign_decls = collect_foreign_declarations(decls, expos_set, sm, doc);

    // And the members named by the bodies this pipeline never renders,
    // collected here for the same reason and carried the same way — the one
    // thing design §9's leakage checker needs that no node can hold.
    const std::vector<ir::BodyUse> body_uses = collect_unextracted_uses(decls, sm);

    // A header synopsis is an ordinary .syn section whose source range
    // is closed by an exact Doxygen `/// END [stable]` line. Fold that bounded
    // range into one ordinary SynopsisDecl before the pipeline goes clang-free;
    // no header-shaped IR node or backend path is needed.
    std::vector<db::DocEvent> events;
    const auto                classify_one = [&](const RawItem& item) {
        return classify(item,
                        sm,
                        doc,
                        lang_opts,
                        omit_set,
                        expos_set,
                        seebelow_map,
                        freestanding_map,
                        ns_drop_set,
                        skipped,
                        attached_comments,
                        source_markup);
    };
    const auto boundary_diagnostic = [&](const RawItem& opener, std::string message) {
        const auto [file, local] = doc.locate(opener.offset);
        events.push_back(db::Ignored{{db::Diagnostic{beman::specgen::Severity::Warning,
                                                     sm.getLineNumber(file, local),
                                                     std::move(message),
                                                     diagnostic_file_name(sm, file)}}});
    };

    // substrate generic algorithm: a stateful source-order fold whose steps
    // consume either one ordinary item or a complete bounded synopsis range.
    for (std::size_t i = 0; i < raw_items.size();) {
        const RawItem&               opener = raw_items[i];
        std::optional<SectionHeader> header;
        if (opener.decl == nullptr)
            if (const auto parsed_header = parse_rsec(opener.comment_text))
                header = parsed_header->value;
        if (!header || !std::string_view(header->stable).ends_with(".syn")) {
            events.push_back(classify_one(opener));
            ++i;
            continue;
        }

        std::optional<std::size_t>     close_index;
        std::optional<SectionEndFence> close;
        std::optional<std::string>     boundary_error;
        // substrate generic algorithm: bounded lookahead for the first
        // structural conflict or exact matching END fence.
        for (std::size_t j = i + 1; j < raw_items.size(); ++j) {
            const RawItem& candidate = raw_items[j];
            if (candidate.decl != nullptr)
                continue;
            if (const auto nested = parse_rsec(candidate.comment_text); nested) {
                boundary_error = std::format(
                    "header synopsis [{}] is not closed before section [{}]", header->stable, nested->value.stable);
                break;
            }
            const auto end = section_end_fence(candidate.comment_text);
            if (!end)
                continue;
            if (end->stable != header->stable) {
                boundary_error =
                    std::format("header synopsis [{}] has mismatched END [{}]", header->stable, end->stable);
                break;
            }
            close_index = j;
            close       = end;
            break;
        }

        events.push_back(classify_one(opener));
        if (!close_index) {
            boundary_diagnostic(
                opener,
                boundary_error.value_or(std::format("header synopsis [{}] has no matching END", header->stable)));
            ++i;
            continue;
        }

        db::SynopsisDecl gathered;
        gathered.offset = opener.offset + 1;
        // The furthest end offset of any declaration this fold has consumed.
        // A comment before it sits *inside* that declaration's extracted
        // text — an in-class `\ref` group header, most commonly — and the
        // extraction already carries it, so appending it again as a
        // standalone group header rendered it twice (issue #31).
        unsigned consumed_end = 0;
        // What a folded-in declaration keeps that the gathered node cannot
        // hold. For a class: its class-general paragraph (design §5.2) and its
        // own description (issue #18), when an `\at` on the class has not
        // routed them out through `pending` already (issue #98) -- unrouted,
        // both belong beside their class's synopsis, in whatever section is
        // open, and the gathered node has one slot for each while a region may
        // hold several classes, so they travel on as their own events instead
        // of being merged (issue #41). For a namespace entity: its whole
        // wording, when nothing routes it elsewhere (issue #69). Pushed after
        // the gathered node, in source order, which is where their offsets
        // place them anyway.
        std::vector<db::DocEvent> extras;
        // The stable name of the `\ref` group header standing over the
        // declaration being folded, or empty. The in-class equivalent
        // (collect_inclass_items) reads the same headers out of the class
        // body; here the fold walks past them in source order, so the last one
        // seen is the one in force.
        std::string current_ref;
        // substrate generic algorithm: a source-order fold composing semantic
        // CodeText fragments while the outer cursor skips the consumed range.
        for (std::size_t j = i + 1; j <= *close_index; ++j) {
            RawItem item = raw_items[j];
            if (j == *close_index)
                item.comment_text.resize(close->line_begin);

            if (item.decl != nullptr) {
                const clang::SourceLocation decl_end_tok =
                    clang::Lexer::getLocForEndOfToken(item.decl->getEndLoc(), 0, sm, lang_opts);
                consumed_end                              = std::max(consumed_end, doc.offset(sm, decl_end_tok));
                const lowering::ItemDirectives directives = docblock_directives(item.decl, sm);
                db::DocEvent                   classified = classify_one(item);
                if (auto* synopsis = std::get_if<db::SynopsisDecl>(&classified);
                    synopsis != nullptr && !synopsis->synopsis.code.text.empty()) {
                    append_synopsis_code(gathered.synopsis.code, std::move(synopsis->synopsis.code));
                    gathered.diagnostics.append_range(std::move(synopsis->diagnostics));
                    // The class's routed in-class members ride along. They
                    // are not part of the gathered synopsis -- build_tree
                    // scatters each to the `\rSec` its `\ref` names, the
                    // same as for a class outside the region -- but the fold
                    // owns the only event they can travel on, and taking the
                    // code alone dropped them and their descriptions with it
                    // (issue #34), leaving the target section empty.
                    gathered.pending.append_range(std::move(synopsis->pending));
                    // And the coverage roster, which the gathered node used to
                    // carry none of: a class folded into a region was not
                    // coverage-checked at all, and `--validate` could not say
                    // so because the evidence never reached it (issue #45).
                    // Each entry names the class that declared it, so several
                    // classes' entries share this node without a finding
                    // losing track of whose member it is about.
                    gathered.synopsis.roster.append_range(std::move(synopsis->synopsis.roster));
                    // Only the class's own *unrouted* wording travels this
                    // way, in an event built for it rather than the classified
                    // one with its taken fields left behind: append_range over
                    // an rvalue container copies, so forwarding the whole event
                    // would scatter every routed member and report every
                    // finding a second time. An `\at` on the class emptied
                    // both fields into `pending` before the fold saw them, so
                    // this pushes nothing then and the wording rides the
                    // gathered node's own pending list to the section it named
                    // (issue #98). build_tree pushes no node for the synopsis
                    // itself, whose code really has been moved out.
                    if (synopsis->general || !synopsis->descr.elements.empty()) {
                        db::SynopsisDecl class_extra;
                        class_extra.offset  = synopsis->offset;
                        class_extra.general = std::move(synopsis->general);
                        class_extra.descr   = std::move(synopsis->descr);
                        extras.push_back(std::move(class_extra));
                    }
                } else {
                    if (auto* ignored = std::get_if<db::Ignored>(&classified))
                        gathered.diagnostics.append_range(std::move(ignored->diagnostics));
                    if (auto* item_decl = std::get_if<db::ItemDecl>(&classified)) {
                        // A misspelled tag is a typo whether or not the
                        // wording it belongs to finds a home, and this is the
                        // only place left to report it from.
                        gathered.diagnostics.append_range(std::move(item_decl->diagnostics));
                        // The region takes the declaration. It used to take
                        // the description with it and drop it (issue #69) --
                        // silently, and with no roster entry behind it, so
                        // coverage could not report the loss either. A
                        // described entity's wording travels instead, the way
                        // a folded-in class's members do: routed by `\at`,
                        // else by the `\ref` group header standing over it,
                        // to the section that should hold it. A range adaptor
                        // object is the case with no way around this -- a
                        // variable with an initializer has no out-of-line
                        // definition to carry its description into a later
                        // section, and moving its declaration out of the
                        // region would take it out of the header synopsis,
                        // where the draft puts it. Unrouted, the wording rides
                        // out beside the synopsis, which is where it lexically
                        // is. A declaration with nothing to say contributes
                        // only its declaration, exactly as before: a masked
                        // customization point object is the common case.
                        if (!item_decl->item.descr.elements.empty()) {
                            // An out-of-line member definition belongs to its
                            // class's group, not to whatever namespace-scope
                            // header was last seen (issue #77): in the house
                            // style the definition carries the wording, and
                            // its `\ref` group is written in the class body
                            // beside the declaration.
                            std::string inherited = current_ref;
                            if (const clang::FunctionDecl* fn = as_out_of_line_member(item.decl)) {
                                const clang::FunctionDecl* in_class = fn->getFirstDecl();
                                if (const auto* record =
                                        llvm::dyn_cast<clang::CXXRecordDecl>(in_class->getLexicalDeclContext()))
                                    inherited = ClassRefGroups(record, sm, lang_opts)
                                                    .section_for(sm.getDecomposedLoc(in_class->getBeginLoc()).second);
                            }
                            const std::string section = directives.at_anchor.value_or(inherited);
                            if (section.empty()) {
                                extras.push_back(std::move(*item_decl));
                            } else {
                                // The roster entry is what makes a route
                                // checkable: build_tree drops a pending item
                                // whose section no `\rSec` opens, and the
                                // entry is the only record that the request
                                // was made (§9's dangling-route rule, which
                                // reads exactly this). Without it a typo in an
                                // `\at` or a `\ref` header loses the wording
                                // as silently as not routing it at all did,
                                // and a namespace entity has no other roster
                                // entry to be caught by.
                                const auto* named = llvm::dyn_cast<clang::NamedDecl>(item.decl);
                                gathered.synopsis.roster.push_back(ir::SynopsisEntry{
                                    named != nullptr ? named->getNameAsString() : std::string{},
                                    ir::Disposition::Routed,
                                    section,
                                    llvm::isa<clang::FunctionDecl>(item.decl) ? ir::MemberKind::Function
                                                                              : ir::MemberKind::Data});
                                gathered.pending.push_back(
                                    db::PendingItem{section, item_decl->placement_key, std::move(item_decl->item)});
                            }
                        }
                    }
                    if (!suppresses_generated_entity(directives) && is_first_declaration(item.decl)) {
                        // The marker masks the declaration here exactly as it
                        // does outside the region (issue #55): a
                        // customization point object belongs in the header
                        // synopsis, so a masked variable is inside one by
                        // construction, and that is where the mask stopped
                        // being applied.
                        AttachedItem marked;
                        attach_docblock(marked, item.decl, sm);
                        const VariableMask mask =
                            variable_seebelow_mask(item.decl, marked.directives, marked.grouping_line);
                        std::optional<clang::SourceRange> see_below;
                        if (marked.directives.seebelow && !marked.directives.seebelow_target)
                            see_below = definition_mask_range(item.decl, sm, lang_opts);
                        append_synopsis_code(
                            gathered.synopsis.code,
                            extract_header_declaration(
                                item.decl, sm, lang_opts, ns_drop_set, expos_set, mask.unspecified, see_below));
                    }
                }
                continue;
            }
            if (item.comment_text.empty() || item.offset < consumed_end)
                continue;

            db::DocEvent classified = classify_one(item);
            if (auto* synopsis = std::get_if<db::SynopsisDecl>(&classified)) {
                append_synopsis_code(gathered.synopsis.code, std::move(synopsis->synopsis.code));
                gathered.diagnostics.append_range(std::move(synopsis->diagnostics));
                continue;
            }

            // Of ordinary comments, only draft synopsis group headers are
            // semantic here. Markup, Doxygen, and namespace-closing comments
            // are source scaffolding and do not enter the gathered node.
            std::string refs;
            std::size_t line_begin = 0;
            // substrate generic algorithm: scan physical lines so only exact
            // draft Ref headers survive from a possibly merged RawComment.
            while (line_begin <= item.comment_text.size()) {
                const std::size_t      newline  = item.comment_text.find('\n', line_begin);
                const std::size_t      line_end = newline == std::string::npos ? item.comment_text.size() : newline;
                const std::string_view line(item.comment_text.data() + line_begin, line_end - line_begin);
                if (line_vocabulary(llvm::StringRef{line}.ltrim(" \t")) == CommentVocabulary::Draft) {
                    if (const std::optional<std::string> ref = parse_ref(line)) {
                        current_ref = *ref;
                        if (!refs.empty())
                            refs.push_back('\n');
                        refs.append(line);
                    }
                }
                if (newline == std::string::npos)
                    break;
                line_begin = newline + 1;
            }
            append_synopsis_code(gathered.synopsis.code, header_comment_code(std::move(refs)));
        }
        events.push_back(std::move(gathered));
        events.append_range(std::move(extras));
        i = *close_index + 1;
    }

    // Stages 2-3: build_tree() folds the `\rSec` frame stack into a tree,
    // grouping each frame's own `\also`/empty-descr followers onto their
    // primaries (via group_items()) in push order as that frame closes —
    // see document_build.hpp's top-of-file note for why stage 3 runs there
    // rather than as a separate pass over the finished tree. Both clang-free,
    // both unit-tested with synthetic events in
    // tests/beman/specgen/document_build.test.cpp.
    // Last, so a gathered region is enclosed once, as a whole, rather than
    // once per class folded into it.
    std::ranges::for_each(events, [](db::DocEvent& event) {
        if (auto* synopsis = std::get_if<db::SynopsisDecl>(&event); synopsis != nullptr && !synopsis->exact)
            enclose_in_namespace_std(synopsis->synopsis.code);
    });

    db::BuildResult built               = db::build_tree(events);
    built.document.paper_entities       = paper_entities;
    built.document.foreign_namespaces   = foreign;
    built.document.foreign_declarations = foreign_decls;
    built.document.unextracted_uses     = body_uses;
    // A source file's bodies join the header's, sorted and deduplicated as
    // the header's own are (issue #176).
    built.document.unextracted_uses.append_range(
        source_uses | std::views::transform([](const auto& use) { return ir::BodyUse{use.first, use.second}; }));
    std::ranges::sort(built.document.unextracted_uses, [](const ir::BodyUse& a, const ir::BodyUse& b) {
        return std::tie(a.function, a.member) < std::tie(b.function, b.member);
    });
    const auto duplicates =
        std::ranges::unique(built.document.unextracted_uses, [](const ir::BodyUse& a, const ir::BodyUse& b) {
            return a.function == b.function && a.member == b.member;
        });
    built.document.unextracted_uses.erase(duplicates.begin(), duplicates.end());
    built.diagnostics.insert(built.diagnostics.begin(), source_diagnostics.begin(), source_diagnostics.end());
    std::vector<std::string> sources = parse_sources(header_path, *parsed.ast);
    sources.append_range(std::move(source_reads));
    return DocumentBuild{std::move(built), std::move(sources)};
}

} // namespace beman::specgen::frontend
