// src/beman/specgen/frontend/inclass_items.cpp                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/inclass_items.hpp>

#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/docblock.hpp>
#include <beman/specgen/document_build.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/lower.hpp>
#include <beman/specgen/markers.hpp>
#include <beman/specgen/section_markers.hpp>
#include <beman/specgen/span_sentinels.hpp>
#include <beman/specgen/synopsis_edits.hpp>
#include <beman/specgen/frontend/docblock_lookup.hpp>
#include <beman/specgen/frontend/file_views.hpp>
#include <beman/specgen/frontend/record_members.hpp>
#include <beman/specgen/frontend/attach.hpp>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclFriend.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RawCommentList.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/TypeBase.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Token.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>

#include <algorithm>
#include <cstddef>
#include <format>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

using grammar::parse_ref;
using span_sentinels::recover_sentinels;
using span_sentinels::span_sentinel;
using span_sentinels::SpanInfo;
using synopsis_edits::add_dominant_edit;
using synopsis_edits::sort_back_to_front;
using synopsis_edits::SynopsisEdit;

namespace {

// The findings for a class-body docblock that can produce no wording of its
// own: the bodyless declaration of a member defined elsewhere, or a non-static
// or exposition-only data member. Markers that act on the declaration --
// `\omit`, `\merge`, `\elsewhere`, `\expos`, `\seebelow`, `\freestanding` -- are
// what such a docblock is for, and stay silent. A description element, or a marker only a
// definition's wording can carry out, asks for wording this docblock cannot
// produce, and dropping it silently is how a header validates clean while its
// descriptions vanish (issues #125, #140): that is an Error saying `problem`.
// The block's grammar findings are reported either way, since nothing else
// reads this docblock.
std::vector<beman::specgen::document_build::Diagnostic>
declaration_docblock_findings(const clang::Decl* decl, const clang::SourceManager& sm, std::string problem) {
    const clang::RawComment* rc = attached_raw_comment(decl);
    if (rc == nullptr)
        return {};
    const llvm::StringRef            raw   = rc->getRawText(sm);
    const std::optional<std::size_t> start = docblock_start(raw);
    if (!start)
        return {};

    const grammar::ParseResult parsed  = grammar::parse_docblock(raw.substr(*start).str());
    const lowering::Lowered    lowered = lowering::lower(parsed.block);
    const grammar::Markers&    markers = lowered.directives;
    const bool asks_for_wording = !lowered.descr.elements.empty() || parsed.block.verbatim_itemdecl ||
                                  markers.verbatim_synopsis || markers.describe || markers.also || markers.group_id ||
                                  markers.constraints_in_decl || markers.impdef || markers.at_anchor ||
                                  markers.effects_equiv || markers.returns_expr;

    std::vector<beman::specgen::document_build::Diagnostic> out = docblock_diagnostics(rc, *start, parsed.diags, sm);
    if (asks_for_wording)
        out.push_back(
            beman::specgen::document_build::Diagnostic{beman::specgen::Severity::Error,
                                                       sm.getSpellingLineNumber(decl->getBeginLoc()),
                                                       std::move(problem),
                                                       diagnostic_file_name(sm, sm.getFileID(decl->getBeginLoc()))});
    return out;
}

// What the docblock on the bodyless in-class declaration of `fn` should have
// been written on instead. The definition places a member's wording (design
// §3.3); one in another file of the translation unit is named by that file,
// and one in a source file named with --source by `source_definition`, its
// `file:line` (issue #149). With neither, the definition is somewhere no
// parse specgen makes can reach, such as a source file not named.
std::string misplaced_declaration_markup(const clang::FunctionDecl*  fn,
                                         const clang::SourceManager& sm,
                                         std::string_view            source_definition = {}) {
    const std::string          name       = fn->getNameAsString();
    const clang::FunctionDecl* definition = fn->getDefinition();
    if (!source_definition.empty())
        return std::format("the documented declaration of `{}` produces no wording: markup belongs at its definition, "
                           "{}, which places a member's wording (design §3.3)",
                           name,
                           source_definition);
    if (definition == nullptr)
        return std::format("the documented declaration of `{}` produces no wording: markup is read from a member's "
                           "definition (design §3.3), and this translation unit has none; name the source file that "
                           "defines it with --source",
                           name);
    const clang::SourceLocation at   = definition->getBeginLoc();
    const std::string           file = diagnostic_file_name(sm, sm.getFileID(at));
    return std::format("the documented declaration of `{}` produces no wording: markup belongs at its definition, "
                       "{}{}{}, which places a member's wording (design §3.3)",
                       name,
                       file.empty() ? "line " : file,
                       file.empty() ? "" : ":",
                       sm.getSpellingLineNumber(at));
}

} // namespace

ClassRefGroups::ClassRefGroups(const clang::CXXRecordDecl* record,
                               const clang::SourceManager& sm,
                               const clang::LangOptions&   lang_opts) {
    const unsigned              class_begin = sm.getDecomposedLoc(record->getBeginLoc()).second;
    const clang::SourceLocation end_of_brace =
        clang::Lexer::getLocForEndOfToken(record->getEndLoc(), 0, sm, lang_opts);
    const unsigned class_end = sm.getDecomposedLoc(end_of_brace).second;

    // ascending by offset -- getCommentsInFile is offset-keyed, and
    // neither the filter nor the transform below reorders.
    if (const std::map<unsigned, clang::RawComment*>* comments =
            file_comments(record->getASTContext(), sm, record->getBeginLoc())) {
        groups_ = *comments | std::views::filter([&](const auto& kv) {
            return kv.first >= class_begin && kv.first < class_end && parse_ref(kv.second->getRawText(sm)).has_value();
        }) | std::views::transform([&](const auto& kv) {
            return std::pair<unsigned, std::string>{kv.first, *parse_ref(kv.second->getRawText(sm))};
        }) | std::ranges::to<std::vector<std::pair<unsigned, std::string>>>();
    }
}

std::string ClassRefGroups::section_for(unsigned offset) const {
    const auto it = std::ranges::partition_point(groups_, [&](const auto& rg) { return rg.first <= offset; });
    return it == groups_.begin() ? std::string{} : std::prev(it)->second;
}

void collect_inclass_items(const clang::CXXRecordDecl*                               record,
                           const clang::SourceManager&                               sm,
                           const clang::LangOptions&                                 lang_opts,
                           const std::set<std::string>&                              ns_drop_set,
                           const std::map<const clang::Decl*, std::string>&          expos_set,
                           const SkippedRanges&                                      skipped,
                           const SourceMarkupMap&                                    source_markup,
                           std::vector<beman::specgen::document_build::PendingItem>& pending,
                           std::vector<beman::specgen::document_build::Diagnostic>&  diagnostics) {
    const ClassRefGroups ref_groups(record, sm, lang_opts);
    const auto           section_for = [&ref_groups](unsigned offset) { return ref_groups.section_for(offset); };

    // substrate generic algorithm: whether a member is collected, and what it
    // is collected as, is a five-step conditional computation -- unwrap it,
    // require a real body or defaulted/deleted, require a docblock, call
    // attach_function, then two more directive-driven skip conditions decide
    // whether the resulting AttachedItem is actually pushed -- over
    // class-body declaration order that the pending list's placement is
    // contractually keyed on. This is a filter-map with no
    // std::ranges name for it; the optional pattern that would fake one reads
    // worse than this loop, the same call extract_synopsis's docblock-comment
    // walk makes.
    bool previous_was_routed_alias = false;
    // substrate generic algorithm: the alias adjacency state is part of this filter-map.
    for (const clang::Decl* member : record->decls()) {
        if (const auto* alias = llvm::dyn_cast<clang::TypeAliasDecl>(member)) {
            const bool adjacent_to_alias = previous_was_routed_alias;
            previous_was_routed_alias    = false;
            if (!has_docblock(alias, sm))
                continue;
            // An `\expos` alias is exposition, not a routed wording item:
            // it renders in the synopsis under its exposid name, and its
            // roster entry is Expos, the same treatment expos data gets.
            if (expos_set.contains(alias->getCanonicalDecl()))
                continue;

            AttachedItem attached = attach_alias(alias, sm, lang_opts, ns_drop_set, expos_set);
            diagnostics.append_range(std::move(attached.diagnostics));
            if (suppresses_generated_entity(attached.directives))
                continue;

            const std::string section = attached.directives.at_anchor.value_or(section_for(attached.inclass_offset));
            pending.push_back(
                beman::specgen::document_build::PendingItem{section,
                                                            attached.inclass_offset,
                                                            std::move(attached.item),
                                                            true,
                                                            attached.directives.also && adjacent_to_alias});
            previous_was_routed_alias = true;
            continue;
        }
        previous_was_routed_alias = false;

        clang::SourceLocation      friend_begin; // keeps `friend` in the itemdecl
        const clang::FunctionDecl* fn = member_function_or_template(member);
        if (fn != nullptr) {
            if (const auto* friend_decl = llvm::dyn_cast<clang::FriendDecl>(member))
                friend_begin = friend_decl->getBeginLoc();
        }
        if (fn == nullptr) {
            // A documented static data member is an item of its own, routed
            // like an alias (issue #148), unless it is `\expos`: exposition
            // renders in the synopsis alone.
            if (llvm::isa<clang::VarDecl, clang::VarTemplateDecl>(member) && has_docblock(member, sm) &&
                !expos_set.contains(member->getCanonicalDecl())) {
                AttachedItem attached = attach_static_member(
                    llvm::cast<clang::NamedDecl>(member), record, sm, lang_opts, ns_drop_set, expos_set);
                diagnostics.append_range(std::move(attached.diagnostics));
                if (suppresses_generated_entity(attached.directives))
                    continue;
                const std::string section =
                    attached.directives.at_anchor.value_or(section_for(attached.inclass_offset));
                pending.push_back(beman::specgen::document_build::PendingItem{
                    section, attached.inclass_offset, std::move(attached.item)});
                continue;
            }
            // Any other data member's specification is its declaration and
            // its class's description (design §6), so a docblock on one has
            // no itemdescr to become (issue #125).
            if (llvm::isa<clang::FieldDecl, clang::VarDecl, clang::VarTemplateDecl>(member) &&
                has_docblock(member, sm))
                diagnostics.append_range(declaration_docblock_findings(
                    member,
                    sm,
                    std::format("a documented data member produces no wording: describe `{}` in its class's "
                                "description",
                                llvm::cast<clang::NamedDecl>(member)->getNameAsString())));
            continue;
        }
        // An in-class member is collected if it has a real (compound) body — a
        // hidden friend or a compiler-bug member — or is `= default`/`= delete`
        // or pure virtual with no definition (a candidate for `\describe`,
        // checked after lowering): those three are declarations with nothing
        // else to carry their markup. Any other bodyless declaration belongs
        // to a definition elsewhere, attached from the top-level stream
        // instead, and a docblock on it produces nothing (issue #140) --
        // unless that definition is in a source file, whose item was built in
        // the source file's parse and is placed here (issue #149).
        // (isOutOfLine() is deliberately *not* checked — a hidden friend is
        // lexically in the class but semantically in the enclosing namespace,
        // so it reports out-of-line yet is exactly what this pass must
        // collect.)
        const bool is_defaulted_or_deleted = fn->isDefaulted() || fn->isDeleted();
        const bool is_bare_pure_virtual    = fn->isPureVirtual() && fn->getDefinition() == nullptr;
        const bool has_compound_body       = fn->doesThisDeclarationHaveABody() && !is_defaulted_or_deleted;
        if (!has_compound_body && !is_defaulted_or_deleted && !is_bare_pure_virtual) {
            const auto        sourced    = source_markup.find(fn->getCanonicalDecl());
            const std::string definition = sourced != source_markup.end() ? sourced->second->where() : std::string{};
            if (has_docblock(fn, sm))
                diagnostics.append_range(
                    declaration_docblock_findings(fn, sm, misplaced_declaration_markup(fn, sm, definition)));
            if (sourced == source_markup.end())
                continue;
            AttachedItem                attached   = sourced->second->attached;
            const clang::SourceLocation anchor_loc = friend_begin.isValid() ? friend_begin : fn->getBeginLoc();
            attached.inclass_offset                = sm.getDecomposedLoc(anchor_loc).second;
            attached.file                          = sm.getFileID(anchor_loc);
            diagnostics.append_range(std::move(attached.diagnostics));
            if (suppresses_generated_entity(attached.directives))
                continue;
            const std::string section = attached.directives.at_anchor.value_or(section_for(attached.inclass_offset));
            pending.push_back(beman::specgen::document_build::PendingItem{
                section, attached.inclass_offset, std::move(attached.item)});
            continue;
        }
        if (!has_docblock(fn, sm))
            continue;

        AttachedItem attached = attach_function(fn, fn, friend_begin, sm, lang_opts, ns_drop_set, expos_set, skipped);
        // The member's docblock findings, taken before any of the
        // skip conditions below: whether this member ends up with wording is
        // a separate question from whether its markup parsed cleanly, and
        // three of the four ways out of this loop are `continue`.
        diagnostics.append_range(std::move(attached.diagnostics));
        // A `= default`/`= delete` member, or a pure virtual one with no
        // definition, earns an itemdecl only when there is
        // something to specify about it — an authored description element
        // (design §6, cf. tuple's defaulted copy constructor, whose itemdecl
        // exists because a Mandates and an Effects apply to it) — or an explicit
        // `\describe`. Otherwise it stays a synopsis-only declaration. The
        // `= default`/`= delete`/`= 0` tail is kept verbatim in the itemdecl. A
        // derive_constraints/derive_mandates element doesn't count: it restates
        // the declaration's own constraint rather than adding something to
        // specify, and on a deleted overload a standalone item for it is
        // actively misleading (issue #121).
        const bool has_authored_element =
            std::ranges::any_of(attached.item.descr.elements, [](const auto& element) { return !element.derived; });
        if ((is_defaulted_or_deleted || is_bare_pure_virtual) && !has_authored_element &&
            !attached.directives.describe)
            continue;
        // `\omit`/`\merge`: no itemdescr (the synopsis line is dropped by the
        // omit-set pre-pass).
        if (suppresses_generated_entity(attached.directives))
            continue;
        // Placement: `\at <stable>` overrides the inferred `\ref` group.
        const std::string section = attached.directives.at_anchor.value_or(section_for(attached.inclass_offset));
        pending.push_back(
            beman::specgen::document_build::PendingItem{section, attached.inclass_offset, std::move(attached.item)});
    }
}

void group_adjacent_aliases(std::vector<beman::specgen::document_build::PendingItem>& pending) {
    namespace db = beman::specgen::document_build;
    std::vector<db::PendingItem> grouped;
    grouped.reserve(pending.size());
    // substrate generic algorithm: a follower mutates its preceding routed primary.
    for (db::PendingItem& candidate : pending) {
        if (candidate.is_alias && candidate.wants_join && !grouped.empty() && grouped.back().is_alias &&
            grouped.back().stable == candidate.stable) {
            // `is_alias` is set only where a marked alias's SpecItem was
            // pushed, so it is also the record of which alternative of the
            // pending payload this item holds (decision
            // routed-wording-payload); a class's own routed wording is never
            // an alias and never reaches here.
            db::append_grouped_itemdecl(std::get<ir::SpecItem>(grouped.back().item).decl,
                                        std::move(std::get<ir::SpecItem>(candidate.item).decl));
            continue;
        }
        grouped.push_back(std::move(candidate));
    }
    pending = std::move(grouped);
}

} // namespace beman::specgen::frontend
