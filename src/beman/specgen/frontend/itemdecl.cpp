// src/beman/specgen/frontend/itemdecl.cpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/itemdecl.hpp>

#include <beman/specgen/ir.hpp>
#include <beman/specgen/span_sentinels.hpp>
#include <beman/specgen/synopsis_edits.hpp>
#include <beman/specgen/frontend/declaration_masks.hpp>
#include <beman/specgen/frontend/draft_format.hpp>
#include <beman/specgen/frontend/merged_twins.hpp>
#include <beman/specgen/frontend/qualifiers.hpp>
#include <beman/specgen/frontend/record_members.hpp>
#include <beman/specgen/frontend/requires_clauses.hpp>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/TypeBase.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Token.h>
#include <llvm/ADT/StringRef.h>

#include <cstddef>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

using span_sentinels::span_sentinel;
using span_sentinels::SpanInfo;
using synopsis_edits::add_dominant_edit;
using synopsis_edits::sort_back_to_front;
using synopsis_edits::SynopsisEdit;

beman::specgen::ir::CodeText extract_itemdecl(const clang::FunctionDecl*                       in_class,
                                              const clang::SourceManager&                      sm,
                                              const clang::LangOptions&                        lang_opts,
                                              bool                                             strip_requires_clause,
                                              const std::set<std::string>&                     ns_drop_set,
                                              const std::map<const clang::Decl*, std::string>& expos_set,
                                              clang::SourceLocation                            friend_begin,
                                              std::optional<SeeBelowTarget>                    seebelow) {
    // A member template's own `template <...>` head is part of its itemdecl
    // (the draft shows it), but a bare FunctionDecl's range starts at the
    // return type/name — the head lives on the described FunctionTemplateDecl.
    // Extract from that when present so the head is kept. (The enclosing class
    // template's head is not on this decl, so it is correctly excluded.)
    const clang::Decl* range_decl = in_class;
    if (const clang::FunctionTemplateDecl* described = in_class->getDescribedFunctionTemplate())
        range_decl = described;

    const clang::SourceLocation begin_loc   = friend_begin.isValid() ? friend_begin : range_decl->getBeginLoc();
    const unsigned              decl_begin  = sm.getDecomposedLoc(begin_loc).second;
    const llvm::StringRef       source_text = clang::Lexer::getSourceText(
        clang::CharSourceRange::getTokenRange(begin_loc, range_decl->getEndLoc()), sm, lang_opts);

    std::string text;
    // As in extract_synopsis, an ODR-used explicitly defaulted member
    // can carry a synthesized body whose source range is not a real body.
    if (in_class->doesThisDeclarationHaveABody() && !in_class->isDefaulted() && !in_class->isDeleted()) {
        const clang::Stmt* body       = in_class->getBody();
        const unsigned     body_begin = spliced_tail_begin(in_class, body, sm);
        text                          = source_text.substr(0, body_begin - decl_begin).str();
    } else {
        text = source_text.str();
    }

    const clang::AssociatedConstraint& requires_clause = in_class->getTrailingRequiresClause();
    if (strip_requires_clause && requires_clause.ConstraintExpr != nullptr) {
        const unsigned req_begin = sm.getDecomposedLoc(requires_clause.ConstraintExpr->getBeginLoc()).second;
        text                     = text.substr(0, req_begin - decl_begin);
        // The whitespace set matches std::isspace under the classic "C"
        // locale (the only locale this text is ever scanned in), stated
        // directly rather than through a per-character predicate call --
        // same spelling docblock.cpp's trim() uses.
        const auto right_trim = [&text] {
            constexpr std::string_view whitespace = " \t\n\v\f\r";
            text.resize(text.find_last_not_of(whitespace) + 1);
        };
        right_trim();
        static constexpr std::string_view kRequires = "requires";
        if (text.size() >= kRequires.size() &&
            text.compare(text.size() - kRequires.size(), kRequires.size(), kRequires) == 0) {
            text.resize(text.size() - kRequires.size());
            right_trim();
        }
    }
    text += ';';

    // Rewrites over the extracted text, in absolute offsets: the `\seebelow`
    // target sentinel and droppable namespace qualifiers.
    // Both sit before whatever the steps above trimmed from the end, so their
    // offsets into `text` remain valid; anything past the trim is discarded by
    // the bounds check below.
    std::map<std::string, SpanInfo> sentinels;
    std::vector<SynopsisEdit>       edits;
    std::optional<SynopsisEdit>     seebelow_edit;

    if (seebelow) {
        if (const auto range = seebelow_source_range(in_class, *seebelow)) {
            const unsigned begin = sm.getDecomposedLoc(range->getBegin()).second;
            const unsigned end =
                sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(range->getEnd(), 0, sm, lang_opts)).second;
            if (begin >= decl_begin && end > begin) {
                const std::string sentinel = span_sentinel(0);
                sentinels[sentinel]        = SpanInfo{beman::specgen::ir::SpanKind::SeeBelow, "SEEBELOW", ""};
                seebelow_edit              = SynopsisEdit{begin, end, sentinel};
            }
        }
    }
    edits.append_range(
        namespace_qualifier_edits(const_cast<clang::Decl*>(range_decl), ns_drop_set, sm, lang_opts, expos_set) |
        std::views::filter([&](const auto& p) { return p.first >= decl_begin; }) |
        std::views::transform([](const auto& p) { return SynopsisEdit{p.first, p.second, ""}; }));
    unsigned span_n = sentinels.size();
    if (const auto marked = expos_set.find(in_class->getCanonicalDecl()); marked != expos_set.end()) {
        const clang::SourceRange name_range = in_class->getNameInfo().getSourceRange();
        const unsigned           name_begin = sm.getDecomposedLoc(name_range.getBegin()).second;
        const unsigned           name_end =
            sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(name_range.getEnd(), 0, sm, lang_opts)).second;
        const std::string sentinel = span_sentinel(span_n++);
        sentinels[sentinel]        = SpanInfo{ir::SpanKind::ExposId, marked->second, marked->second};
        edits.push_back(SynopsisEdit{name_begin, name_end, sentinel});
    }
    // substrate generic algorithm: the same stateful sentinel/edit flat-map
    // as the synopsis path above, scoped to this declaration fragment.
    for (const ExposUse& use : expos_uses(const_cast<clang::Decl*>(range_decl), expos_set, sm, lang_opts)) {
        if (use.name_begin < decl_begin)
            continue;
        const std::string sentinel = span_sentinel(span_n++);
        sentinels[sentinel]        = SpanInfo{beman::specgen::ir::SpanKind::ExposId, use.display, use.display};
        edits.push_back(SynopsisEdit{use.name_begin, use.name_end, sentinel});
        if (use.qualifier_end > use.qualifier_begin)
            edits.push_back(SynopsisEdit{use.qualifier_begin, use.qualifier_end, ""});
    }
    // The head requires-clause (design §5.1, issue #119): an interior excision,
    // not the tail truncation above. Delete from just past the parameter list's
    // `>` through the end of the clause, so the `requires` keyword and the
    // whitespace before it go with it and no token scan is needed. Dominant,
    // because a qualifier or exposition-only edit that landed inside the clause
    // sorts ahead of this one and would otherwise trip the descending
    // applier's overlap watermark, suppressing the deletion entirely.
    if (strip_requires_clause) {
        if (const clang::Expr* head_clause = head_requires_clause(in_class)) {
            const clang::SourceLocation rangle = function_head_parameters(in_class)->getRAngleLoc();
            const unsigned              clause_begin =
                sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(rangle, 0, sm, lang_opts)).second;
            const unsigned clause_end =
                sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(head_clause->getEndLoc(), 0, sm, lang_opts))
                    .second;
            if (clause_begin >= decl_begin && clause_end > clause_begin)
                add_dominant_edit(edits, SynopsisEdit{clause_begin, clause_end, " "});
        }
    }
    if (seebelow_edit)
        add_dominant_edit(edits, std::move(*seebelow_edit));
    // A kept trailing clause says only what a pair of merged twins shares.
    if (!strip_requires_clause) {
        // substrate generic algorithm: each dominant edit erases the nested
        // edits it overlaps, as for the see-below edit above.
        for (SynopsisEdit& edit : twin_cut_edits(in_class, sm, lang_opts))
            add_dominant_edit(edits, std::move(edit));
    }

    // Back to front, skipping overlaps and anything past the trimmed end.
    sort_back_to_front(edits);
    unsigned applied_begin = std::numeric_limits<unsigned>::max();
    // substrate generic algorithm: the same watermark discipline as
    // extract_synopsis's applier above, and for the same reason -- a
    // mutation whose correctness depends on descending position is a
    // scatter, not a fold in disguise.
    for (const SynopsisEdit& edit : edits) {
        if (edit.end > applied_begin)
            continue;
        const std::size_t rel_begin = edit.begin - decl_begin;
        const std::size_t rel_end   = edit.end - decl_begin;
        if (rel_end > text.size())
            continue; // fell in the spliced-away body or requires-clause
        text.replace(rel_begin, rel_end - rel_begin, edit.replacement);
        applied_begin = edit.begin;
    }

    return format_and_recover(std::move(text), sentinels);
}

beman::specgen::ir::CodeText extract_alias_itemdecl(const clang::TypeAliasDecl*                      alias,
                                                    const clang::SourceManager&                      sm,
                                                    const clang::LangOptions&                        lang_opts,
                                                    const std::set<std::string>&                     ns_drop_set,
                                                    const std::map<const clang::Decl*, std::string>& expos_set,
                                                    std::optional<AliasMask>                         mask,
                                                    const clang::Decl*                               head) {
    namespace ir = beman::specgen::ir;

    const clang::SourceLocation begin_loc  = head != nullptr ? head->getBeginLoc() : alias->getBeginLoc();
    const unsigned              decl_begin = sm.getDecomposedLoc(begin_loc).second;
    // Through the declaration's last token, not the type's: they differ when
    // the RHS ends in a cv-qualifier, and the semicolon is added back here.
    std::string text =
        clang::Lexer::getSourceText(
            clang::CharSourceRange::getTokenRange(begin_loc, alias_last_token(alias, sm, lang_opts)), sm, lang_opts)
            .str();
    text += ';';

    std::map<std::string, SpanInfo> sentinels;
    std::vector<SynopsisEdit>       edits;
    edits.append_range(
        namespace_qualifier_edits(const_cast<clang::TypeAliasDecl*>(alias), ns_drop_set, sm, lang_opts, expos_set) |
        std::views::filter([&](const auto& range) { return range.first >= decl_begin; }) |
        std::views::transform([](const auto& range) { return SynopsisEdit{range.first, range.second, ""}; }));

    unsigned span_n = 0;
    // substrate generic algorithm: one AST-use rewrite may emit two ordered edits.
    for (const ExposUse& use : expos_uses(const_cast<clang::TypeAliasDecl*>(alias), expos_set, sm, lang_opts)) {
        if (use.name_begin < decl_begin)
            continue;
        const std::string sentinel = span_sentinel(span_n++);
        sentinels[sentinel]        = SpanInfo{ir::SpanKind::ExposId, use.display, use.display};
        edits.push_back(SynopsisEdit{use.name_begin, use.name_end, sentinel});
        if (use.qualifier_end > use.qualifier_begin)
            edits.push_back(SynopsisEdit{use.qualifier_begin, use.qualifier_end, ""});
    }

    if (mask) {
        if (const auto range = alias_rhs_source_range(alias, sm, lang_opts)) {
            const unsigned begin = sm.getDecomposedLoc(range->getBegin()).second;
            const unsigned end =
                sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(range->getEnd(), 0, sm, lang_opts)).second;
            if (begin >= decl_begin && end > begin) {
                const std::string  sentinel = span_sentinel(span_n++);
                const ir::SpanKind kind =
                    *mask == AliasMask::ImplDefined ? ir::SpanKind::ImplDefined : ir::SpanKind::SeeBelow;
                const std::string display = *mask == AliasMask::ImplDefined ? "implementation-defined" : "SEEBELOW";
                sentinels[sentinel]       = SpanInfo{kind, display, ""};
                add_dominant_edit(edits, SynopsisEdit{begin, end, sentinel});
            }
        }
    }

    sort_back_to_front(edits);
    unsigned applied_begin = std::numeric_limits<unsigned>::max();
    // substrate generic algorithm: descending edits share the synopsis watermark discipline.
    for (const SynopsisEdit& edit : edits) {
        if (edit.end > applied_begin)
            continue;
        const std::size_t begin = edit.begin - decl_begin;
        const std::size_t end   = edit.end - decl_begin;
        if (end > text.size())
            continue;
        text.replace(begin, end - begin, edit.replacement);
        applied_begin = edit.begin;
    }
    return format_and_recover(std::move(text), sentinels);
}

} // namespace beman::specgen::frontend
