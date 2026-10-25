// src/beman/specgen/frontend/frontend.cpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/frontend.hpp>

#include <beman/specgen/conjuncts.hpp>
#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/docblock.hpp>
#include <beman/specgen/document_build.hpp>
#include <beman/specgen/parse/cursor.hpp>
#include <beman/specgen/parse/parser.hpp>
#include <beman/specgen/frontend/build_failure.hpp>
#include <beman/specgen/frontend/compile_args.hpp>
#include <beman/specgen/frontend/decl_projections.hpp>
#include <beman/specgen/frontend/declaration_masks.hpp>
#include <beman/specgen/frontend/docblock_lookup.hpp>
#include <beman/specgen/frontend/document_files.hpp>
#include <beman/specgen/frontend/draft_format.hpp>
#include <beman/specgen/frontend/expos_spellings.hpp>
#include <beman/specgen/frontend/file_views.hpp>
#include <beman/specgen/frontend/merged_twins.hpp>
#include <beman/specgen/frontend/parse_header.hpp>
#include <beman/specgen/frontend/qualifiers.hpp>
#include <beman/specgen/frontend/record_members.hpp>
#include <beman/specgen/frontend/requires_clauses.hpp>
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
#include <clang/AST/OperationKinds.h>
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
#include <iterator>
#include <limits>
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

// --- redeclaration-chain attachment (design §3.3) ---------------------------

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
                                              std::optional<SeeBelowTarget> seebelow = std::nullopt) {
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
                                                    const clang::Decl*                               head = nullptr) {
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

// --- Constraints derivation (design §5.1) -----------------------------------
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

// Fold a formatted fragment onto one line. A conjunct is inline code inside a
// sentence, so it has to be one: org's `~…~` does not span two newlines, and a
// markdown span that does is one blank line from breaking. clang-format keeps
// the author's line breaks here (ColumnLimit 0, in format_expr_fragment) and
// a multi-line requires-expression is an ordinary way to write one, so fold
// them out.
// Only a whitespace run containing a newline folds, which leaves the spacing
// inside a string literal as written. This runs before recover_sentinels,
// whose span offsets must be the folded text's; a sentinel is an identifier
// with no whitespace in it, so folding cannot reach inside one.
//
// A folded run becomes one space, except where a bracket hugs it: after an
// opening `(`, `[` or template `<`, and before a closing `)`, `]` or template
// `>`, it goes entirely, or `convertible_to<\n T, int>` read
// `convertible_to< T, int>` (issue #167). clang-format spaces a binary `<`
// and `>`, and writes a template's brackets against the name and the argument,
// so a `<` straight after a name and a `>` with no space after it are the
// template's. Braces are not brackets here: a requires-expression's body is
// written spaced, `{ { t == 0 }; }`.
std::string fold_lines(std::string_view text) {
    // The whitespace set the trims elsewhere in this file use.
    static constexpr std::string_view whitespace = " \t\n\v\f\r";
    const auto                        opens      = [](std::string_view before) {
        if (before.empty())
            return false;
        const char last = before.back();
        if (last == '(' || last == '[')
            return true;
        return last == '<' && before.size() > 1 && is_ident_char(before[before.size() - 2]);
    };
    const auto closes = [](std::string_view after) {
        if (after.empty())
            return false;
        const char first = after.front();
        if (first == ')' || first == ']')
            return true;
        const std::size_t past = after.find_first_not_of('>');
        return first == '>' && (past == std::string_view::npos || (after[past] != ' ' && after[past] != '='));
    };
    std::string out;
    out.reserve(text.size());
    std::size_t pos = 0;
    // substrate generic algorithm: what a run folds to depends on the text
    // either side of it, which a run-at-a-time transform cannot see.
    while (pos < text.size()) {
        const std::size_t end = std::min(text.find_first_not_of(whitespace, pos), text.size());
        if (end == pos) {
            out.push_back(text[pos++]);
            continue;
        }
        const std::string_view run = text.substr(pos, end - pos);
        if (!run.contains('\n') && !run.contains('\r'))
            out.append(run);
        else if (!opens(text.substr(0, pos)) && !closes(text.substr(end)))
            out.push_back(' ');
        pos = end;
    }
    return out;
}

// The written text of `range`, rewritten the way every code fragment is: the
// drop set's qualifiers removed, exposition-only uses replaced by their exposid
// spans, the draft's qualifier order applied, and the lines folded. The edits
// are found by walking `root`, which must contain `range`; an edit outside the
// range is discarded. The two differ for a concept-id's sole argument, which
// is a written template argument and not an expression of its own: walking
// the whole ConceptSpecializationExpr reaches it, and the range keeps only its
// text (issue #126). Whitespace at either end of the range is trimmed.
//
// `expos_names` is the class-local textual fallback (design §3.5) that an
// extracted body gets too: the uses of an enclosing class's exposition-only
// members, which a dependent member access can leave unresolved. Derived
// conjuncts pass none, since a constraint names no class member.
beman::specgen::ir::CodeText code_rewritten(const clang::Stmt*                               root,
                                            clang::CharSourceRange                           range,
                                            const clang::SourceManager&                      sm,
                                            const clang::LangOptions&                        lang_opts,
                                            const std::set<std::string>&                     ns_drop_set,
                                            const std::map<const clang::Decl*, std::string>& expos_set,
                                            const std::map<std::string, std::string>&        expos_names = {}) {
    static constexpr std::string_view whitespace = " \t\n\v\f\r";
    auto* const                       walked     = const_cast<clang::Stmt*>(root);
    std::string                       text       = clang::Lexer::getSourceText(range, sm, lang_opts).str();
    const std::size_t                 lead       = std::min(text.find_first_not_of(whitespace), text.size());
    text.erase(0, lead);
    text.erase(text.find_last_not_of(whitespace) + 1);
    const unsigned            text_begin = sm.getDecomposedLoc(range.getBegin()).second + static_cast<unsigned>(lead);
    std::vector<SynopsisEdit> edits =
        namespace_qualifier_edits(walked, ns_drop_set, sm, lang_opts, expos_set) |
        std::views::transform([](const auto& p) { return SynopsisEdit{p.first, p.second, ""}; }) |
        std::ranges::to<std::vector<SynopsisEdit>>();
    std::map<std::string, SpanInfo> sentinels;
    unsigned                        span_n = 0;
    // substrate generic algorithm: build a sentinel table and one-or-two edits
    // per resolved use; the shared counter couples both output collections.
    for (const ExposUse& use : expos_uses(walked, expos_set, sm, lang_opts)) {
        if (use.plain) {
            edits.push_back(SynopsisEdit{use.name_begin, use.name_end, use.display});
            continue;
        }
        const std::string sentinel = span_sentinel(span_n++);
        sentinels[sentinel]        = SpanInfo{beman::specgen::ir::SpanKind::ExposId, use.display, use.display};
        edits.push_back(SynopsisEdit{use.name_begin, use.name_end, sentinel});
        if (use.qualifier_end > use.qualifier_begin)
            edits.push_back(SynopsisEdit{use.qualifier_begin, use.qualifier_end, ""});
    }
    sort_back_to_front(edits);
    unsigned applied_begin = std::numeric_limits<unsigned>::max();
    // substrate generic algorithm: descending offset mutation with an overlap
    // watermark, identical in kind to the synopsis/itemdecl edit applicators.
    for (const SynopsisEdit& edit : edits) {
        if (edit.begin < text_begin || edit.end > text_begin + text.size() || edit.end > applied_begin)
            continue;
        text.replace(edit.begin - text_begin, edit.end - edit.begin, edit.replacement);
        applied_begin = edit.begin;
    }
    text = apply_expos_sentinels(std::move(text), expos_names, span_n, sentinels);
    return recover_sentinels(fold_lines(format_expr_fragment(text)), sentinels);
}

beman::specgen::ir::CodeText expr_code_rewritten(const clang::Expr*                               e,
                                                 const clang::SourceManager&                      sm,
                                                 const clang::LangOptions&                        lang_opts,
                                                 const std::set<std::string>&                     ns_drop_set,
                                                 const std::map<const clang::Decl*, std::string>& expos_set,
                                                 const std::map<std::string, std::string>&        expos_names = {}) {
    return code_rewritten(e,
                          clang::CharSourceRange::getTokenRange(e->getSourceRange()),
                          sm,
                          lang_opts,
                          ns_drop_set,
                          expos_set,
                          expos_names);
}

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
                                              const std::map<const clang::Decl*, std::string>& expos_set) {
    namespace ir = beman::specgen::ir;

    const clang::Expr* peeled = leaf->IgnoreParenImpCasts();

    if (const auto* concept_expr = llvm::dyn_cast<clang::ConceptSpecializationExpr>(peeled)) {
        const clang::ConceptDecl*                 concept_decl = concept_expr->getNamedConcept();
        const std::string                         name         = concept_decl->getName().str();
        const clang::ASTTemplateArgumentListInfo* args         = concept_expr->getTemplateArgsAsWritten();
        ir::Paragraph                             out;
        if (args != nullptr && args->getNumTemplateArgs() == 1) {
            // "`arg` models \libconcept{name}". The sole argument is usually a
            // type (`copyable<U>`), so its text is everything between the
            // angle brackets: a qualified type's own source range starts after
            // a leading `const`, which `small<const T&>` lost as `T&`. The text
            // is rewritten as the value form's code is, or a `std::` qualifier
            // and an exposition-only name survived into the subject (issue
            // #126).
            const clang::CharSourceRange subject = clang::CharSourceRange::getCharRange(
                clang::Lexer::getLocForEndOfToken(args->getLAngleLoc(), 0, sm, lang_opts), args->getRAngleLoc());
            out.push_back(
                ir::CodeInline{code_rewritten(concept_expr, subject, sm, lang_opts, ns_drop_set, expos_set)});
            out.push_back(ir::TextInline{" models "});
            if (const auto found = expos_set.find(concept_decl->getCanonicalDecl()); found != expos_set.end()) {
                out.push_back(ir::CodeInline{
                    ir::CodeText{found->second, {{0, found->second.size(), ir::SpanKind::ExposId, found->second}}}});
            } else {
                out.push_back(ir::ConceptRef{name});
            }
        } else {
            // No single subject to front; keep the whole concept-id verbatim.
            out.push_back(ir::CodeInline{expr_code_rewritten(peeled, sm, lang_opts, ns_drop_set, expos_set)});
            out.push_back(ir::TextInline{" is satisfied"});
        }
        return out;
    }

    const clang::UnaryOperator* negation = llvm::dyn_cast<clang::UnaryOperator>(peeled);
    if (negation != nullptr && negation->getOpcode() != clang::UO_LNot)
        negation = nullptr;

    const clang::Expr* code_expr = negation != nullptr ? negation->getSubExpr() : peeled;

    ir::Paragraph out;
    out.push_back(ir::CodeInline{expr_code_rewritten(code_expr, sm, lang_opts, ns_drop_set, expos_set)});
    out.push_back(ir::TextInline{" is "});
    out.push_back(ir::CodeInline{ir::CodeText{negation != nullptr ? "false" : "true", {}}});
    return out;
}

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
                   const std::map<const clang::Decl*, std::string>& expos_set) {
    namespace ir = beman::specgen::ir;

    std::vector<const clang::Expr*> leaves;
    if (const clang::Expr* head_clause = head_requires_clause(fn))
        split_conjuncts(head_clause, leaves);
    // The trailing clause less what only tells `fn` apart from a merged twin.
    const std::vector<const clang::Expr*> trailing = trailing_conjuncts(fn);
    const std::vector<bool>               cut      = twin_cut_mask(fn, sm, lang_opts);
    leaves.append_range(
        std::views::zip(trailing, std::views::iota(std::size_t{0})) |
        std::views::filter([&cut](const auto& leaf) { return cut.empty() || !cut[std::get<1>(leaf)]; }) |
        std::views::elements<0>);
    if (leaves.empty())
        return std::nullopt;

    const std::vector<ir::Paragraph> paragraphs =
        leaves | std::views::transform([&](const clang::Expr* leaf) {
            return phrase_conjunct(leaf, sm, lang_opts, ns_drop_set, expos_set);
        }) |
        std::ranges::to<std::vector<ir::Paragraph>>();

    ir::DescriptionElement element;
    element.kind      = ir::ElementKind::Constraints;
    element.derived   = true;
    element.conjuncts = paragraphs;
    beman::specgen::conjuncts::render_into(paragraphs, element);
    return element;
}

// --- Mandates derivation (design §5.2) --------------------------------------
// [structure.specifications] read backwards: Mandates = ill-formed (not merely
// removed from overload resolution) = the leading static_assert prefix of the
// out-of-line definition body. Consume the *maximal* run of static_asserts at
// the front of the body, flatten each condition at top-level `&&` (reusing
// split_conjuncts), and phrase every conjunct with the same rewriter
// Constraints uses — design §5.3 shares both the phrasing and the
// sentence/itemize rendering across the two derivations. Leading local aliases
// may precede the assertion run; the first statement of any other kind ends the
// prologue, so a static_assert appearing later is not a Mandate. static_assert
// *messages* are dropped, as the design specifies.
//
// This only tags the element `derived` and keeps its pre-join
// conjuncts (ir.hpp's DescriptionElement::conjuncts) -- ordering an authored
// twin after it (canonicalize, ir.cpp), merging the two into one rendered
// block (backend/latex.cpp), and the drift check that compares them
// (validate.cpp) are Tier-A code, not repeated here. Excluding the consumed
// asserts from `\effects-equiv` extraction is shared below with the body
// extraction.

const clang::Decl* single_decl(const clang::Stmt* stmt) {
    const auto* decl_stmt = llvm::dyn_cast<clang::DeclStmt>(stmt);
    return decl_stmt != nullptr && decl_stmt->isSingleDecl() ? decl_stmt->getSingleDecl() : nullptr;
}

struct MandatesPrologue {
    std::vector<const clang::DeclStmt*> assertions;
    const clang::Stmt*                  extraction_first = nullptr;
};

// One shared reading of the body prologue for both Mandates derivation and
// Equivalent-to extraction. Leading local aliases are retained source, but
// permit the immediately following maximal static_assert run to remain
// derivable. The first declaration/statement of any other kind closes the
// prologue; an assertion after that boundary is ordinary body code.
MandatesPrologue mandates_prologue(const clang::CompoundStmt* body) {
    const std::vector<const clang::Stmt*> stmts(body->body_begin(), body->body_end());
    std::size_t                           pos = 0;
    // substrate generic algorithm: the shared cursor partitions one source
    // sequence into aliases, assertions, and the unconsumed remainder.
    while (pos < stmts.size() && llvm::isa_and_nonnull<clang::TypeAliasDecl>(single_decl(stmts[pos])))
        ++pos;
    const std::size_t alias_count = pos;

    MandatesPrologue result;
    // substrate generic algorithm: continue the same partitioning cursor while
    // recording the middle run for both derivation and source deletion.
    while (pos < stmts.size() && llvm::isa_and_nonnull<clang::StaticAssertDecl>(single_decl(stmts[pos]))) {
        result.assertions.push_back(llvm::cast<clang::DeclStmt>(stmts[pos]));
        ++pos;
    }

    if (result.assertions.empty())
        result.extraction_first = stmts.empty() ? nullptr : stmts.front();
    else if (alias_count != 0)
        result.extraction_first = stmts.front();
    else if (pos < stmts.size())
        result.extraction_first = stmts[pos];
    return result;
}

std::optional<beman::specgen::ir::DescriptionElement>
derive_mandates(const clang::FunctionDecl*                       fn,
                const clang::SourceManager&                      sm,
                const clang::LangOptions&                        lang_opts,
                const std::set<std::string>&                     ns_drop_set,
                const std::map<const clang::Decl*, std::string>& expos_set) {
    namespace ir = beman::specgen::ir;

    const auto* body = llvm::dyn_cast_or_null<clang::CompoundStmt>(fn->getBody());
    if (body == nullptr)
        return std::nullopt;

    // The conjuncts of one static_assert's condition, flattened at top-level
    // `&&` (split_conjuncts).
    const auto conjuncts_of = [](const clang::DeclStmt* stmt) {
        std::vector<const clang::Expr*> out;
        const auto*                     assert_decl = llvm::cast<clang::StaticAssertDecl>(stmt->getSingleDecl());
        split_conjuncts(assert_decl->getAssertExpr(), out);
        return out;
    };

    const MandatesPrologue                prologue = mandates_prologue(body);
    const std::vector<const clang::Expr*> leaves   = prologue.assertions | std::views::transform(conjuncts_of) |
                                                     std::views::join |
                                                     std::ranges::to<std::vector<const clang::Expr*>>();
    if (leaves.empty())
        return std::nullopt;

    const std::vector<ir::Paragraph> paragraphs =
        leaves | std::views::transform([&](const clang::Expr* leaf) {
            return phrase_conjunct(leaf, sm, lang_opts, ns_drop_set, expos_set);
        }) |
        std::ranges::to<std::vector<ir::Paragraph>>();

    ir::DescriptionElement element;
    element.kind      = ir::ElementKind::Mandates;
    element.derived   = true;
    element.conjuncts = paragraphs;
    beman::specgen::conjuncts::render_into(paragraphs, element);
    return element;
}

// --- class-head static_asserts (design §5.2) --------------------------------

bool is_template_parameter_pack(const clang::NamedDecl* param) {
    if (const auto* type = llvm::dyn_cast<clang::TemplateTypeParmDecl>(param))
        return type->isParameterPack();
    if (const auto* value = llvm::dyn_cast<clang::NonTypeTemplateParmDecl>(param))
        return value->isParameterPack();
    if (const auto* tmpl = llvm::dyn_cast<clang::TemplateTemplateParmDecl>(param))
        return tmpl->isParameterPack();
    return false;
}

// Spell the injected class-id from the declaration names Clang resolved. The
// condition text still comes from source tokens; this small synthesized name
// is wording context ("instantiates `optional<T>`"), not synopsis code.
std::string class_instantiation_name(const clang::CXXRecordDecl* record) {
    const clang::ClassTemplateDecl* tmpl = record->getDescribedClassTemplate();
    if (tmpl == nullptr)
        return record->getNameAsString();

    std::vector<std::string> arguments;
    // substrate generic algorithm: this is a transform with an early return
    // from the enclosing function when any parameter cannot be named; a view
    // cannot express that fallback without materializing an error channel.
    for (const clang::NamedDecl* param : *tmpl->getTemplateParameters()) {
        std::string name = param->getNameAsString();
        // An unnamed parameter has no source-level identifier with which to
        // spell an injected class-id. Keep the class name rather than invent
        // one; named parameters are the Beman-style input convention.
        if (name.empty())
            return record->getNameAsString();
        if (is_template_parameter_pack(param))
            name += "...";
        arguments.push_back(std::move(name));
    }
    if (arguments.empty())
        return record->getNameAsString();

    return std::format("{}<{}>",
                       record->getNameAsString(),
                       arguments | std::views::join_with(std::string_view(", ")) | std::ranges::to<std::string>());
}

// The class-scope static_assert derivation (design §5.2): one adjacent
// general-subclause paragraph, plus the conjuncts it was folded from. The
// conjuncts are the same validator-only drift evidence a member's derived
// Mandates carries -- an authored class `\mandates` replaces the paragraph
// and inherits them, so drift detection still sees the assertions the
// authored text is standing in for.
struct ClassMandates {
    beman::specgen::ir::FreeParagraph          text;
    std::vector<beman::specgen::ir::Paragraph> conjuncts;
};

std::optional<ClassMandates> derive_class_mandates(const clang::CXXRecordDecl*                      record,
                                                   const clang::SourceManager&                      sm,
                                                   const clang::LangOptions&                        lang_opts,
                                                   const std::set<std::string>&                     ns_drop_set,
                                                   const std::map<const clang::Decl*, std::string>& expos_set) {
    namespace ir = beman::specgen::ir;

    std::vector<const clang::Expr*> leaves;
    // All direct assertions belong to this class's general wording, wherever
    // an access label happens to occur. Nested records are separate synopses
    // and are deliberately not recursed into.
    // substrate generic algorithm: a filtered flat-map into the shared leaves
    // accumulator; split_conjuncts recursively appends zero or more results.
    for (const clang::Decl* member : record->decls()) {
        const auto* assert_decl = llvm::dyn_cast<clang::StaticAssertDecl>(member);
        if (assert_decl != nullptr)
            split_conjuncts(assert_decl->getAssertExpr(), leaves);
    }
    if (leaves.empty())
        return std::nullopt;

    std::vector<ir::Paragraph> conditions = leaves | std::views::transform([&](const clang::Expr* leaf) {
                                                return phrase_conjunct(leaf, sm, lang_opts, ns_drop_set, expos_set);
                                            }) |
                                            std::ranges::to<std::vector<ir::Paragraph>>();

    ir::Paragraph text;
    text.push_back(ir::TextInline{"A program that instantiates "});
    text.push_back(ir::CodeInline{ir::CodeText{class_instantiation_name(record), {}}});
    text.push_back(ir::TextInline{" is ill-formed unless "});
    text.append_range(beman::specgen::conjuncts::join_sentence(conditions));
    return ClassMandates{ir::FreeParagraph{std::move(text)}, std::move(conditions)};
}

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
                              const std::map<const clang::Decl*, std::string>& expos_set) {
    namespace ir = beman::specgen::ir;

    out.descr = std::move(block.descr);
    // Within the class's own wording, uses of its exposition-only members
    // render as \exposid -- the same rewrite a member's description gets.
    rewrite_prose_expos(out.descr, expos_spellings(record, expos_set));

    std::optional<ClassMandates> derived = derive_class_mandates(record, sm, lang_opts, ns_drop_set, expos_set);
    if (!derived)
        return;
    const auto authored = std::ranges::find_if(out.descr.elements, [](const ir::DescriptionElement& element) {
        return element.kind == ir::ElementKind::Mandates && !element.derived;
    });
    if (authored != out.descr.elements.end())
        authored->conjuncts = std::move(derived->conjuncts);
    else
        out.general = std::move(derived->text);
}

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
                             const beman::specgen::lowering::ItemDirectives& directives) {
    namespace db = beman::specgen::document_build;

    if (!directives.at_anchor || directives.at_anchor->empty())
        return;
    if (!out.general && out.descr.elements.empty())
        return;

    const std::string name = record->getNameAsString();
    out.synopsis.roster.push_back(
        ir::SynopsisEntry{name, ir::Disposition::Routed, *directives.at_anchor, ir::MemberKind::Function, name});

    if (out.general) {
        out.pending.push_back(db::PendingItem{*directives.at_anchor, out.offset, std::move(*out.general)});
        out.general.reset();
    }
    if (!out.descr.elements.empty()) {
        out.pending.push_back(
            db::PendingItem{*directives.at_anchor, out.offset, ir::SpecItem{{}, std::move(out.descr)}});
        out.descr = {};
    }
}

// --- \effects-equiv / \returns-expr body extraction (design §4.2) -----------
// `\effects-equiv` lowers to a description element carrying an *empty*
// EquivalentTo (lowering::append_extraction); the front end fills its code from
// the out-of-line definition body here. Per design §5.2 ("Consumed asserts are
// excluded from `\effects-equiv` extraction"), the prologue assertion run that
// derive_mandates consumes is deleted here too, while any preceding aliases are
// retained. A Mandates element and an Effects "Equivalent to:" block derived
// from one body therefore do not double-count the asserts.

// Source text of `body` from its first retained prologue statement through to
// just before the closing brace, reformatted with the draft FormatStyle.
// Extracting up to getRBracLoc() (rather than the last statement's end token)
// keeps the trailing `;` of each statement, which the statement's own source
// range excludes. Empty if nothing remains (an all-asserts or empty body).
std::vector<std::pair<unsigned, unsigned>> conditional_body_edits(
    const llvm::StringRef& buffer, clang::FileID file, unsigned begin, unsigned end, const SkippedRanges& skipped) {
    std::vector<std::pair<unsigned, unsigned>> edits;
    // substrate generic algorithm: clip-and-filter into an existing edit set;
    // the optional output makes a plain transform the wrong shape.
    for (const SkippedRange& range : skipped) {
        if (range.file != file)
            continue;
        const unsigned clipped_begin = std::max(begin, range.begin);
        const unsigned clipped_end   = std::min(end, range.end);
        if (clipped_end > clipped_begin)
            edits.emplace_back(clipped_begin, clipped_end);
    }

    const auto is_conditional = [](std::string_view line) {
        constexpr std::string_view whitespace = " \t\r\n";
        const std::size_t          hash       = line.find_first_not_of(whitespace);
        if (hash == std::string_view::npos || line[hash] != '#')
            return false;
        const std::size_t word_begin = line.find_first_not_of(whitespace, hash + 1);
        if (word_begin == std::string_view::npos)
            return false;
        const std::size_t      word_end = line.find_first_of(whitespace, word_begin);
        const std::string_view word     = line.substr(word_begin, word_end - word_begin);
        return word == "if" || word == "ifdef" || word == "ifndef" || word == "elif" || word == "elifdef" ||
               word == "elifndef" || word == "else" || word == "endif";
    };

    // substrate generic algorithm: a position-tracking scan whose next input
    // offset is the newline discovered by the current iteration.
    for (unsigned line_begin = begin; line_begin < end;) {
        const std::size_t found = buffer.find('\n', line_begin);
        const unsigned    line_end =
            found == llvm::StringRef::npos || found >= end ? end : static_cast<unsigned>(found + 1);
        if (is_conditional(std::string_view(buffer.data() + line_begin, line_end - line_begin)))
            edits.emplace_back(line_begin, line_end);
        line_begin = line_end;
    }
    return edits;
}

// Clang's raw-comment table is the lexical boundary: comment spellings inside
// strings never enter it. Block comments have one vocabulary for the whole
// token; merged line comments are classified one physical line at a time.
// Whole-line removals absorb their newline, while trailing removals do not.
std::vector<std::pair<unsigned, unsigned>> body_comment_edits(const clang::ASTContext&    ctx,
                                                              const clang::SourceManager& sm,
                                                              const clang::LangOptions&   lang_opts,
                                                              const llvm::StringRef&      buffer,
                                                              clang::SourceLocation       in_file,
                                                              unsigned                    begin,
                                                              unsigned                    end) {
    std::vector<std::pair<unsigned, unsigned>> edits;
    const auto*                                comments = file_comments(ctx, sm, in_file);
    if (comments == nullptr)
        return edits;

    const auto deletion_range = [&](unsigned comment_begin, unsigned comment_end) {
        const std::size_t previous_newline = buffer.rfind('\n', comment_begin);
        const unsigned    line_begin =
            previous_newline == llvm::StringRef::npos ? 0 : static_cast<unsigned>(previous_newline + 1);
        unsigned deletion_begin = comment_begin;
        // substrate generic algorithm: walk one lexical boundary backward to
        // absorb indentation without inspecting any preceding source line.
        while (deletion_begin > line_begin &&
               (buffer[deletion_begin - 1] == ' ' || buffer[deletion_begin - 1] == '\t'))
            --deletion_begin;

        const bool standalone =
            buffer.substr(line_begin, deletion_begin - line_begin).find_first_not_of(" \t") == llvm::StringRef::npos;
        if (standalone) {
            deletion_begin  = line_begin;
            unsigned suffix = comment_end;
            // substrate generic algorithm: walk the same physical line forward
            // so a whole-line removal consumes its horizontal tail and newline.
            while (suffix < end && (buffer[suffix] == ' ' || buffer[suffix] == '\t'))
                ++suffix;
            if (suffix < end && buffer[suffix] == '\r')
                ++suffix;
            if (suffix < end && buffer[suffix] == '\n')
                comment_end = suffix + 1;
        }
        return std::pair{deletion_begin, comment_end};
    };

    // substrate generic algorithm: a bounded filter-map from Clang's absolute
    // comment table into zero or more deletion ranges for this fragment.
    for (const auto& entry : *comments) {
        const clang::RawComment* comment       = entry.second;
        const clang::SourceRange range         = comment->getSourceRange();
        const unsigned           comment_begin = sm.getDecomposedLoc(range.getBegin()).second;
        const unsigned           comment_end =
            sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(range.getEnd(), 0, sm, lang_opts)).second;
        if (comment_end <= begin || comment_begin >= end)
            continue;

        const llvm::StringRef raw = comment->getRawText(sm);
        if (raw.ltrim(" \t").starts_with("/*")) {
            if (line_vocabulary(raw.ltrim(" \t")) != CommentVocabulary::Draft)
                edits.push_back(deletion_range(comment_begin, comment_end));
            continue;
        }

        // substrate generic algorithm: a position-tracking scan is required
        // because each merged physical line needs its absolute source offset.
        for (std::size_t line_begin = 0; line_begin < raw.size();) {
            const std::size_t     newline  = raw.find('\n', line_begin);
            const std::size_t     line_end = newline == llvm::StringRef::npos ? raw.size() : newline;
            const llvm::StringRef line     = raw.slice(line_begin, line_end).ltrim(" \t");
            if (line_vocabulary(line) != CommentVocabulary::Draft) {
                const unsigned absolute_begin = comment_begin + static_cast<unsigned>(line_begin);
                const unsigned absolute_end   = comment_begin + static_cast<unsigned>(line_end);
                edits.push_back(deletion_range(absolute_begin, absolute_end));
            }
            if (newline == llvm::StringRef::npos)
                break;
            line_begin = newline + 1;
        }
    }
    return edits;
}

// Normalize absolute source deletion ranges over one extracted fragment.
// Every producer shares this clipping/union step before semantic replacements
// are filtered against the text that will actually survive.
std::vector<std::pair<unsigned, unsigned>>
normalize_body_deletions(unsigned text_begin, unsigned text_size, std::vector<std::pair<unsigned, unsigned>> edits) {
    const unsigned text_end = text_begin + text_size;
    // substrate generic algorithm: normalize the caller-owned interval set in
    // place before the following stateful overlap fold.
    for (auto& [begin, end] : edits) {
        begin = std::max(begin, text_begin);
        end   = std::min(end, text_end);
    }
    std::erase_if(edits, [](const auto& edit) { return edit.second <= edit.first; });
    std::sort(edits.begin(), edits.end());

    std::vector<std::pair<unsigned, unsigned>> merged;
    // substrate generic algorithm: an ordered interval-union fold whose state
    // is the last range already emitted.
    for (const auto& edit : edits) {
        if (!merged.empty() && edit.first <= merged.back().second)
            merged.back().second = std::max(merged.back().second, edit.second);
        else
            merged.push_back(edit);
    }
    return merged;
}

// Apply normalized deletions and semantic replacements together, back to
// front, so every edit remains in the original main-file coordinate system.
std::string apply_body_edits(std::string                                       text,
                             unsigned                                          text_begin,
                             const std::vector<std::pair<unsigned, unsigned>>& deletions,
                             std::vector<SynopsisEdit>                         replacements) {
    replacements.append_range(deletions | std::views::transform([](const auto& deletion) {
                                  return SynopsisEdit{deletion.first, deletion.second, ""};
                              }));
    sort_back_to_front(replacements);

    unsigned applied_begin = text_begin + static_cast<unsigned>(text.size());
    // substrate generic algorithm: descending-offset string mutation with an
    // overlap watermark; later edits determine whether earlier ones survive.
    for (const SynopsisEdit& edit : replacements) {
        if (edit.end > applied_begin)
            continue;
        text.replace(edit.begin - text_begin, edit.end - edit.begin, edit.replacement);
        applied_begin = edit.begin;
    }
    return text;
}

beman::specgen::ir::CodeText extract_equiv_body(const clang::CompoundStmt*                       body,
                                                const clang::ASTContext&                         ctx,
                                                const clang::SourceManager&                      sm,
                                                const clang::LangOptions&                        lang_opts,
                                                const std::map<std::string, std::string>&        expos_names,
                                                const std::map<const clang::Decl*, std::string>& expos_set,
                                                const std::set<std::string>&                     ns_drop_set,
                                                const SkippedRanges&                             skipped) {
    const MandatesPrologue prologue = mandates_prologue(body);
    if (prologue.extraction_first == nullptr)
        return {};

    const unsigned begin = sm.getDecomposedLoc(prologue.extraction_first->getBeginLoc()).second;
    const unsigned end   = sm.getDecomposedLoc(body->getRBracLoc()).second;
    if (end <= begin)
        return {};

    const llvm::StringRef                      buffer = file_buffer(sm, prologue.extraction_first->getBeginLoc());
    std::vector<std::pair<unsigned, unsigned>> deletions =
        namespace_qualifier_edits(const_cast<clang::CompoundStmt*>(body), ns_drop_set, sm, lang_opts, expos_set);
    deletions.append_range(
        conditional_body_edits(buffer, sm.getFileID(prologue.extraction_first->getBeginLoc()), begin, end, skipped));
    deletions.append_range(
        body_comment_edits(ctx, sm, lang_opts, buffer, prologue.extraction_first->getBeginLoc(), begin, end));
    deletions.append_range(prologue.assertions | std::views::transform([&](const clang::DeclStmt* statement) {
                               const unsigned statement_begin = sm.getDecomposedLoc(statement->getBeginLoc()).second;
                               const unsigned statement_end =
                                   sm.getDecomposedLoc(
                                         clang::Lexer::getLocForEndOfToken(statement->getEndLoc(), 0, sm, lang_opts))
                                       .second;
                               return std::pair{statement_begin, statement_end};
                           }));

    const std::vector<ExposUse> semantic_uses =
        expos_uses(const_cast<clang::CompoundStmt*>(body), expos_set, sm, lang_opts);
    deletions.append_range(
        semantic_uses |
        std::views::filter([](const ExposUse& use) { return use.qualifier_end > use.qualifier_begin; }) |
        std::views::transform([](const ExposUse& use) { return std::pair{use.qualifier_begin, use.qualifier_end}; }));
    deletions = normalize_body_deletions(begin, end - begin, std::move(deletions));

    unsigned                        span_n = 0;
    std::map<std::string, SpanInfo> sentinels;
    std::vector<SynopsisEdit>       replacements;
    const auto                      is_deleted = [&](unsigned use_begin, unsigned use_end) {
        return std::ranges::any_of(
            deletions, [&](const auto& deletion) { return use_begin < deletion.second && deletion.first < use_end; });
    };
    // substrate generic algorithm: each surviving semantic use allocates a
    // unique sentinel and appends its coordinated replacement metadata.
    for (const ExposUse& use : semantic_uses) {
        if (use.name_begin < begin || use.name_end > end || is_deleted(use.name_begin, use.name_end))
            continue;
        if (use.plain) {
            replacements.push_back(SynopsisEdit{use.name_begin, use.name_end, use.display});
            continue;
        }
        const std::string sentinel = span_sentinel(span_n++);
        sentinels[sentinel]        = SpanInfo{beman::specgen::ir::SpanKind::ExposId, use.display, use.display};
        replacements.push_back(SynopsisEdit{use.name_begin, use.name_end, sentinel});
    }
    const std::string text =
        apply_body_edits(buffer.substr(begin, end - begin).str(), begin, deletions, std::move(replacements));

    // Dependent member uses can lack a resolved DeclRefExpr; keep design
    // §3.5's class-local textual fallback after the semantic namespace pass.
    std::string sentineled = apply_expos_sentinels(text, expos_names, span_n, sentinels);

    // The range runs up to the closing brace, so it carries the trailing
    // newline/indentation before `}`; drop it. CodeText holds the block body
    // without a trailing newline (as the synopsis does) — render_codeblock
    // supplies the newline before `\end{codeblock}`, so a trailing one here
    // would print a blank line.
    beman::specgen::ir::CodeText out = format_and_recover(std::move(sentineled), sentinels);
    out.text.erase(out.text.find_last_not_of(" \t\n") + 1);
    return out;
}

// Fill every description element that carries an empty EquivalentTo (an
// `\effects-equiv` slot left by lowering) from `fn`'s definition body.
// Elements whose EquivalentTo already has text (e.g. from round-tripped IR)
// are untouched.
void fill_equiv_bodies(const clang::FunctionDecl*                       fn,
                       beman::specgen::ir::ItemDescr&                   descr,
                       const clang::SourceManager&                      sm,
                       const clang::LangOptions&                        lang_opts,
                       const std::map<std::string, std::string>&        expos_names,
                       const std::map<const clang::Decl*, std::string>& expos_set,
                       const std::set<std::string>&                     ns_drop_set,
                       const SkippedRanges&                             skipped) {
    namespace ir = beman::specgen::ir;

    const auto is_empty_slot = [](const ir::DescriptionElement& el) {
        return el.equivalent.has_value() && el.equivalent->code.text.empty();
    };
    if (std::none_of(descr.elements.begin(), descr.elements.end(), is_empty_slot))
        return;

    const auto* body = llvm::dyn_cast_or_null<clang::CompoundStmt>(fn->getBody());
    if (body == nullptr)
        return;

    const ir::CodeText code =
        extract_equiv_body(body, fn->getASTContext(), sm, lang_opts, expos_names, expos_set, ns_drop_set, skipped);
    // substrate generic algorithm: a for_each-shaped mutation of the elements
    // it iterates, not a container rebuild -- filtering states the predicate
    // up front instead of burying it in the loop body's if, but this tree
    // declines ranges::for_each for exactly the reason frontend.cpp's
    // orphaned-label walk does: wrapping the assignment in a
    // call would hide the loop, not remove it.
    //
    // Note the body **falsifies is_empty_slot for the element it just
    // visited**. That is safe, but by specification rather than by
    // construction: filter_view::iterator::operator++ advances past the
    // current element *before* searching for the next match, so the now-
    // non-matching element is never re-tested, and no element is skipped or
    // revisited. Anything that reorders this -- re-reading the filtered range,
    // or caching an iterator across the assignment -- loses that guarantee.
    for (ir::DescriptionElement& el : descr.elements | std::views::filter(is_empty_slot))
        el.equivalent->code = code;
}

// The Returns element `\returns-expr` asks for (design §4.2, issue #143):
// *Returns*: `E`, from a definition body that is one `return E;`. A Returns
// names the value and nothing else -- whatever else the function does is an
// effect, which *Returns*: `E` cannot say -- so any other body is an Error
// pointing at `\effects-equiv`, whose Equivalent to: block carries all of it.
// The static_assert run the Mandates derivation consumes (design §5.2) is
// not part of that body: it is already a Mandates element, not an effect. A
// local alias ahead of it stays retained source, as it does for
// `\effects-equiv`, and so is "any other body".
//
// Engaged with the element, or with the Error's message.
std::expected<beman::specgen::ir::DescriptionElement, std::string>
derive_returns_expr(const clang::FunctionDecl*                       fn,
                    const clang::SourceManager&                      sm,
                    const clang::LangOptions&                        lang_opts,
                    const std::set<std::string>&                     ns_drop_set,
                    const std::map<const clang::Decl*, std::string>& expos_set,
                    const std::map<std::string, std::string>&        expos_names) {
    namespace ir     = beman::specgen::ir;
    const auto* body = llvm::dyn_cast_or_null<clang::CompoundStmt>(fn->getBody());
    if (body == nullptr)
        return std::unexpected(std::string("\\returns-expr reads a definition's body, and this declaration has none"));

    const MandatesPrologue prologue = mandates_prologue(body);
    const auto*            only     = llvm::dyn_cast_or_null<clang::ReturnStmt>(prologue.extraction_first);
    const bool             last     = body->size() != 0 && body->body_back() == prologue.extraction_first;
    if (only == nullptr || !last || only->getRetValue() == nullptr)
        return std::unexpected(std::string("\\returns-expr needs a body that is one `return E;` statement, whose "
                                           "`E` is the value returned; for any other body, use \\effects-equiv"));

    ir::DescriptionElement element;
    element.kind = ir::ElementKind::Returns;
    element.paragraphs.push_back(ir::Paragraph{
        ir::CodeInline{expr_code_rewritten(only->getRetValue(), sm, lang_opts, ns_drop_set, expos_set, expos_names)},
        ir::TextInline{"."}});
    return element;
}

// Result of build_spec_item: the SpecItem it built (possibly empty), the
// directives its docblock carried (default-constructed if it had none), and
// whether it attached an out-of-line function definition at all — the caller
// (classify(), the pipeline's first stage) needs that last bit to decide
// whether `\also`/empty-descr grouping and `\omit` apply; a non-function
// decl's empty SpecItem must never be treated as a group primary or a
// candidate for omission.
struct AttachedItem {
    beman::specgen::ir::SpecItem             item;
    beman::specgen::lowering::ItemDirectives directives;
    bool                                     is_function_def = false;
    // Whatever this item's docblock grammar reported, positioned in
    // the main file. Carried out with the item rather than printed here: the
    // front end builds IR and says nothing to a terminal, so reporting is the
    // driver's, reached through document_build::BuildResult::diagnostics.
    std::vector<beman::specgen::document_build::Diagnostic> diagnostics;
    // Placement key: the byte offset of the item's *in-class*
    // declaration within the main file — getFirstDecl() for an out-of-line
    // definition, the friend/member itself for an in-class one. Both are class-
    // body offsets, so ordering items by this key is class-body order (design
    // §3.3), the axis that interleaves in-class members with their out-of-line
    // siblings.
    unsigned inclass_offset = 0;
    // The file `inclass_offset` is an offset in.  A document can span several
    // files (issue #77), and placement keys are compared across all of them,
    // so the pair is what identifies a position -- item_decl_event turns it
    // into the document offset that ordering actually uses.
    clang::FileID file;
    unsigned      grouping_line = 0; // first line of the docblock carrying grouping metadata
    // \verbatim-itemdecl payload (design §4.3, issue #4): exact, span-free
    // text that *replaces* the extracted declaration — the attach path that
    // would extract one uses this instead when it is engaged, and classify()
    // suppresses the comment's own standalone node so the item appears once.
    std::optional<std::string> verbatim_itemdecl = {};
};

// A documented out-of-line member definition in a source file (issue #149),
// built into its item in the source file's own parse: markup, the body's
// derivations and the itemdecl, exactly as a definition in the header is. The
// header's parse places it, by the in-class declaration it defines, with
// whatever `\ref` group or `\at` routes that declaration. Only the parts of
// `attached` that hold no Clang state cross between the two parses; the header
// side replaces `file` and `inclass_offset` with its own.
struct SourceMarkup {
    AttachedItem attached;
    // The definition's position, for messages naming it.
    std::string file;
    unsigned    line = 0;

    std::string where() const { return std::format("{}:{}", file, line); }
};

// The source files' items, by the canonical in-class declaration each defines
// in the header's parse.
using SourceMarkupMap = std::map<const clang::Decl*, const SourceMarkup*>;

// What bare `\seebelow` on a namespace-scope variable (template) asks for: the
// declared type masked as the draft's *unspecified* placeholder and the
// initializer dropped — the customization-point-object shape (issue #24). The
// targeted forms have no variable meaning, so one is an Error rather than the
// silent no-op the bare form used to be.
//
// Both variable paths read the marker through here. On an ordinary wording item
// it is the itemdecl that carries the mask; marked `\expos` as well, the two
// compose (issue #38) and the standalone synopsis carries it, an
// exposition-only object of unspecified type being how the draft spells a
// helper of this kind. Read on only one of the two paths, the other one
// silently ignored the marker.
struct VariableMask {
    const clang::VarDecl*                                     unspecified = nullptr;
    std::optional<beman::specgen::document_build::Diagnostic> diagnostic  = {};
};

VariableMask variable_seebelow_mask(const clang::Decl*                              decl,
                                    const beman::specgen::lowering::ItemDirectives& directives,
                                    unsigned                                        grouping_line) {
    // An enumeration is the one documented namespace entity with nothing for
    // `\seebelow` to write: a variable masks its type, an alias and a concept
    // their definitions, and the draft never spells an enum-base or an
    // enumerator list *see below*. Reported rather than ignored, because the
    // marker's whole job is to make the wording say less than the code, and a
    // marker that silently does nothing leaves a *Remarks* claiming something
    // the synopsis beside it contradicts.
    if (llvm::isa<clang::EnumDecl>(decl) && directives.seebelow && grouping_line != 0)
        return {nullptr,
                beman::specgen::document_build::Diagnostic{
                    beman::specgen::Severity::Error, grouping_line, "an enumeration accepts no \\seebelow"}};
    const clang::VarDecl* variable = llvm::dyn_cast<clang::VarDecl>(decl);
    if (const auto* var_tmpl = llvm::dyn_cast<clang::VarTemplateDecl>(decl))
        variable = var_tmpl->getTemplatedDecl();
    if (variable == nullptr || !directives.seebelow || grouping_line == 0)
        return {};
    if (directives.seebelow_target)
        return {nullptr,
                beman::specgen::document_build::Diagnostic{
                    beman::specgen::Severity::Error, grouping_line, "a variable accepts only bare \\seebelow"}};
    return {variable, std::nullopt};
}

// Parse and lower `decl`'s own `//!` docblock into `attached` — descr,
// directives, diagnostics, and the grouping line — if it carries one. The
// shared front half of the attach_* functions whose markup and itemdecl come
// off one decl (attach_alias, attach_record_declaration,
// attach_namespace_entity). attach_function keeps its own copy inline: its
// markup decl and itemdecl decl differ, and it reads the directives
// mid-flight to decide requires-clause stripping.
void attach_docblock(AttachedItem& attached, const clang::Decl* decl, const clang::SourceManager& sm) {
    namespace grammar  = beman::specgen::grammar;
    namespace lowering = beman::specgen::lowering;

    const clang::RawComment* rc = attached_raw_comment(decl);
    if (rc == nullptr)
        return;
    const llvm::StringRef            raw      = rc->getRawText(sm);
    const std::optional<std::size_t> start    = docblock_start(raw);
    const llvm::StringRef            raw_text = start ? raw.substr(*start) : llvm::StringRef{};
    if (raw_text.empty())
        return;
    attached.grouping_line = sm.getSpellingLineNumber(rc->getBeginLoc().getLocWithOffset(static_cast<int>(*start)));
    const grammar::ParseResult pr      = grammar::parse_docblock(raw_text.str());
    lowering::Lowered          lowered = lowering::lower(pr.block);
    attached.diagnostics               = docblock_diagnostics(rc, *start, pr.diags, sm);
    attached.item.descr                = std::move(lowered.descr);
    attached.directives                = std::move(lowered.directives);
    attached.verbatim_itemdecl         = pr.block.verbatim_itemdecl;
    if (attached.directives.verbatim_synopsis && attached.grouping_line > 0)
        // The synopsis form belongs on a class definition; on anything an
        // AttachedItem renders it would vanish silently, and silence is the
        // failure mode issue #1 taught this file to refuse.
        attached.diagnostics.push_back({beman::specgen::Severity::Error,
                                        attached.grouping_line,
                                        "\\verbatim-synopsis applies to a class or class template definition"});
}

// `expos_set` plus the renames that keep a definition's code on the itemdecl's
// parameter names (issue #160). The itemdecl is the declaration's text, so a
// definition that spells a parameter differently would otherwise make the
// wording name one parameter two ways. A parameter the declaration leaves
// unnamed has no spelling to rename to and is left alone.
std::map<const clang::Decl*, std::string>
with_parameter_renames(const clang::FunctionDecl*                       def,
                       const clang::FunctionDecl*                       decl_form,
                       const std::map<const clang::Decl*, std::string>& expos_set) {
    std::map<const clang::Decl*, std::string> out = expos_set;
    if (def == decl_form)
        return out;
    // substrate generic algorithm: a pairwise walk that inserts into a map
    // keyed by the definition's parameter, not a transform of either range.
    for (const auto& [definition_param, declared_param] :
         std::views::zip(def->parameters(), decl_form->parameters())) {
        const std::string declared = declared_param->getNameAsString();
        if (!declared.empty() && declared != definition_param->getNameAsString())
            out.emplace(definition_param, declared);
    }
    return out;
}

// Build the AttachedItem for one function, given the decl its markup/body/
// derivations come from (`def`) and the decl its itemdecl *text* comes from
// (`decl_form`). For an out-of-line definition those differ — markup at the
// definition (clause order), itemdecl from the in-class declaration (design
// §3.3) — so `def` is the definition and `decl_form` is `def->getFirstDecl()`.
// For an in-class definition (a hidden friend or a compiler-bug member)
// the definition *is* its own first declaration, so both are the same decl and
// `friend_begin` is the enclosing FriendDecl's begin location (invalid for a
// plain member) so the itemdecl keeps `friend`.
AttachedItem attach_function(const clang::FunctionDecl*                       def,
                             const clang::FunctionDecl*                       decl_form,
                             clang::SourceLocation                            friend_begin,
                             const clang::SourceManager&                      sm,
                             const clang::LangOptions&                        lang_opts,
                             const std::set<std::string>&                     ns_drop_set,
                             const std::map<const clang::Decl*, std::string>& expos_set,
                             const SkippedRanges&                             skipped) {
    namespace ir       = beman::specgen::ir;
    namespace grammar  = beman::specgen::grammar;
    namespace lowering = beman::specgen::lowering;

    AttachedItem attached;
    attached.is_function_def               = true;
    const clang::SourceLocation anchor_loc = friend_begin.isValid() ? friend_begin : decl_form->getBeginLoc();
    attached.inclass_offset                = sm.getDecomposedLoc(anchor_loc).second;
    attached.file                          = sm.getFileID(anchor_loc);

    // Descr: the definition's own `//!` docblock, if it has one.
    // attached_raw_comment yields the immediately preceding comment;
    // is_docblock_comment guards against picking up a draft-form `\ref`/
    // `\rSec` comment that happens to sit just above the definition. Read
    // ahead of the itemdecl (below) because `\constraints-in-decl`, carried
    // on `attached.directives`, decides whether the requires-clause is
    // stripped from it.
    if (const clang::RawComment* rc = attached_raw_comment(def)) {
        const llvm::StringRef            raw      = rc->getRawText(sm);
        const std::optional<std::size_t> start    = docblock_start(raw);
        const llvm::StringRef            raw_text = start ? raw.substr(*start) : llvm::StringRef{};
        if (!raw_text.empty()) {
            attached.grouping_line =
                sm.getSpellingLineNumber(rc->getBeginLoc().getLocWithOffset(static_cast<int>(*start)));
            const grammar::ParseResult pr      = grammar::parse_docblock(raw_text.str());
            lowering::Lowered          lowered = lowering::lower(pr.block);
            // The findings parse_docblock produced — the element-ordering
            // Note, the duplicate-element Warning, the unknown-tag Error —
            // carried out so the driver can print them.
            attached.diagnostics = docblock_diagnostics(rc, *start, pr.diags, sm);
            // \omit and \also (design §4.3) are acted on by classify() and
            // document_build::group_items (pipeline stages 1/3), which have
            // the tree context this function does not. \describe/\at/\merge
            // act on in-class-defined members and remain unread here.
            attached.item.descr        = std::move(lowered.descr);
            attached.directives        = std::move(lowered.directives);
            attached.verbatim_itemdecl = pr.block.verbatim_itemdecl;
            if (attached.directives.impdef) {
                const std::vector<grammar::Diagnostic> invalid{
                    {beman::specgen::Severity::Error, 0, "\\impdef applies only to type aliases"}};
                attached.diagnostics.append_range(docblock_diagnostics(rc, *start, invalid, sm));
            }
            if (attached.directives.verbatim_synopsis) {
                const std::vector<grammar::Diagnostic> invalid{
                    {beman::specgen::Severity::Error,
                     0,
                     "\\verbatim-synopsis applies to a class or class template definition"}};
                attached.diagnostics.append_range(docblock_diagnostics(rc, *start, invalid, sm));
            }
        }
    }

    // Itemdecl: the in-class declaration's own text (design §3.3), one
    // signature per SpecItem here — `\also` grouping appends further
    // signatures onto a group's primary in document_build::group_items, not
    // here. A `\verbatim-itemdecl` payload replaces the extraction whole
    // (design §4.3, issue #4): exact, span-free authored text, so none of the
    // seebelow/requires-stripping machinery applies to it. Otherwise, default
    // (no `\constraints-in-decl`): the requires-clause is stripped, since it
    // is derived into a Constraints element below instead (design §5.1).
    const bool strip_requires = !attached.directives.constraints_in_decl;
    if (attached.verbatim_itemdecl) {
        attached.item.decl.signatures.push_back(ir::CodeText{*attached.verbatim_itemdecl, {}});
    } else {
        attached.item.decl.signatures.push_back(extract_itemdecl(decl_form,
                                                                 sm,
                                                                 lang_opts,
                                                                 strip_requires,
                                                                 ns_drop_set,
                                                                 expos_set,
                                                                 friend_begin,
                                                                 seebelow_target(attached.directives)));
    }
    if (const std::string name = decl_form->getNameAsString(); !name.empty())
        attached.item.decl.entities.push_back(name);

    // The declaration kind determines the draft index form without
    // inspecting its formatted spelling. Use decl_form -- the declaration the
    // itemdecl renders -- rather than the out-of-line definition.
    if (const auto* ctor = llvm::dyn_cast<clang::CXXConstructorDecl>(decl_form)) {
        attached.item.decl.index.push_back({ir::IndexKind::Constructor, ctor->getParent()->getNameAsString(), {}});
    } else if (const auto* dtor = llvm::dyn_cast<clang::CXXDestructorDecl>(decl_form)) {
        attached.item.decl.index.push_back({ir::IndexKind::Destructor, dtor->getParent()->getNameAsString(), {}});
    } else if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(decl_form)) {
        attached.item.decl.index.push_back(
            {ir::IndexKind::Member, method->getNameAsString(), method->getParent()->getNameAsString()});
    } else if (const std::string name = decl_form->getNameAsString(); !name.empty()) {
        attached.item.decl.index.push_back({ir::IndexKind::Global, name, {}});
    }

    // An authored Constraints/Mandates replaces its derived twin. Preserve the
    // derivation's conjuncts on that authored element as validator-only drift
    // evidence; conjuncts are not rendered or leakage-checked.
    const auto attach_derivation = [&](std::optional<ir::DescriptionElement> derived) {
        if (!derived)
            return;
        const auto authored = std::ranges::find_if(attached.item.descr.elements, [&](const auto& element) {
            return element.kind == derived->kind && !element.derived;
        });
        if (authored != attached.item.descr.elements.end())
            authored->conjuncts = std::move(derived->conjuncts);
        else
            attached.item.descr.elements.push_back(std::move(*derived));
        ir::canonicalize(attached.item.descr);
    };

    // Constraints (design §5.1): derive from the definition's own trailing
    // requires-clause unless `\constraints-in-decl` leaves it in the itemdecl.
    const auto body_expos = with_parameter_renames(def, decl_form, expos_set);
    if (strip_requires) {
        attach_derivation(derive_constraints(def, sm, lang_opts, ns_drop_set, body_expos));
    }

    // Mandates (design §5.2): derive from the definition body's static_assert
    // prefix, independent of the requires-clause handling above, and re-fold
    // into canonical [structure.specifications] order.
    attach_derivation(derive_mandates(def, sm, lang_opts, ns_drop_set, body_expos));

    // \effects-equiv (design §4.2): fill the empty EquivalentTo lowering left
    // on the marker's element from the definition body, excluding the
    // static_assert prefix the Mandates derivation above consumed (design
    // §5.2). A no-op when the docblock carried no extraction marker.
    // Expos uses (design §3.5/§4.1): within this class's own fragments,
    // uses of its exposition-only members render as \exposid — in the extracted
    // equiv body, and in backticked prose.
    const std::map<std::string, std::string> expos_names = expos_spellings(enclosing_record(def), expos_set);
    fill_equiv_bodies(def, attached.item.descr, sm, lang_opts, expos_names, body_expos, ns_drop_set, skipped);
    // \returns-expr builds its Returns element from the same body, with the
    // same expos rewriting.
    if (attached.directives.returns_expr) {
        if (auto returns = derive_returns_expr(def, sm, lang_opts, ns_drop_set, body_expos, expos_names)) {
            attached.item.descr.elements.push_back(std::move(*returns));
            ir::canonicalize(attached.item.descr);
        } else {
            attached.diagnostics.push_back(beman::specgen::document_build::Diagnostic{
                beman::specgen::Severity::Error,
                attached.grouping_line,
                std::move(returns.error()),
                diagnostic_file_name(sm, sm.getFileID(def->getBeginLoc()))});
        }
    }
    rewrite_prose_expos(attached.item.descr, expos_names);

    return attached;
}

// `head` is the enclosing TypeAliasTemplateDecl for a namespace-scope alias
// template — the decl the docblock hangs off and the extraction range starts
// at; null for a plain alias, in class or out.
AttachedItem attach_alias(const clang::TypeAliasDecl*                      alias,
                          const clang::SourceManager&                      sm,
                          const clang::LangOptions&                        lang_opts,
                          const std::set<std::string>&                     ns_drop_set,
                          const std::map<const clang::Decl*, std::string>& expos_set,
                          const clang::TypeAliasTemplateDecl*              head = nullptr) {
    const clang::Decl* anchor = head != nullptr ? static_cast<const clang::Decl*>(head) : alias;

    AttachedItem attached;
    attached.inclass_offset = sm.getDecomposedLoc(anchor->getBeginLoc()).second;
    attached.file           = sm.getFileID(anchor->getBeginLoc());

    attach_docblock(attached, anchor, sm);
    if (attached.directives.seebelow_target && attached.grouping_line > 0)
        // Positioned at the docblock's first line, the same place
        // docblock_diagnostics puts a line-0 grammar finding.
        attached.diagnostics.push_back(
            {beman::specgen::Severity::Error, attached.grouping_line, "a type alias accepts only bare \\seebelow"});

    if (attached.verbatim_itemdecl)
        attached.item.decl.signatures.push_back(ir::CodeText{*attached.verbatim_itemdecl, {}});
    else
        attached.item.decl.signatures.push_back(extract_alias_itemdecl(
            alias, sm, lang_opts, ns_drop_set, expos_set, alias_mask(attached.directives), head));
    attached.item.decl.entities.push_back(alias->getNameAsString());
    if (const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(alias->getDeclContext())) {
        attached.item.decl.index.push_back(
            {ir::IndexKind::Member, alias->getNameAsString(), record->getNameAsString()});
    } else if (alias->getDeclContext()->isFileContext()) {
        attached.item.decl.index.push_back({ir::IndexKind::Global, alias->getNameAsString(), {}});
    }
    rewrite_prose_expos(attached.item.descr,
                        expos_spellings(llvm::dyn_cast<clang::CXXRecordDecl>(alias->getDeclContext()), expos_set));
    return attached;
}

// Build the AttachedItem for a documented record declaration the header never
// defines (design §6): an undefined class-template primary — the normal way
// to write an algebra whose operations a model must register, deliberately
// left undefined so an unregistered type fails a concept — or its non-template
// counterpart. Its wording is an ordinary itemdecl: the declaration through
// its semicolon, plus whatever description the docblock carries. `decl` is
// the top-level decl (the ClassTemplateDecl for a template, so the extraction
// starts at `template` and the docblock is looked up where classify's
// definition arms look it up); `record` supplies the class/struct/union tag
// that keeps the template head on its own line. No index metadata: a
// *defined* record's Synopsis carries none either, and the two should index
// alike or not at all.
AttachedItem attach_record_declaration(const clang::NamedDecl*                          decl,
                                       const clang::CXXRecordDecl*                      record,
                                       const clang::SourceManager&                      sm,
                                       const clang::LangOptions&                        lang_opts,
                                       const std::set<std::string>&                     ns_drop_set,
                                       const std::map<const clang::Decl*, std::string>& expos_set) {
    AttachedItem attached;
    attached.inclass_offset = sm.getDecomposedLoc(decl->getBeginLoc()).second;
    attached.file           = sm.getFileID(decl->getBeginLoc());
    attach_docblock(attached, decl, sm);

    const llvm::StringRef tag = record->getKindName();
    if (attached.verbatim_itemdecl)
        attached.item.decl.signatures.push_back(ir::CodeText{*attached.verbatim_itemdecl, {}});
    else
        attached.item.decl.signatures.push_back(
            extract_freestanding_declaration(decl,
                                             sm,
                                             lang_opts,
                                             ns_drop_set,
                                             expos_set,
                                             /*exposition=*/false,
                                             std::string_view(tag.data(), tag.size())));
    attached.item.decl.entities.push_back(decl->getNameAsString());
    return attached;
}

// Build the AttachedItem for a documented namespace-scope concept, variable,
// or variable template (design §6): like the record declaration above, the
// declaration *is* the wording — extracted whole through its semicolon,
// constraint and initializer kept, the way the draft writes them
// ([concept.same], [tuple.helper]). Aliases go through attach_alias instead,
// which owns the alias masking rules; either way the item indexes as a
// library global, the way a documented free function does.
AttachedItem attach_namespace_entity(const clang::NamedDecl*                          decl,
                                     const clang::SourceManager&                      sm,
                                     const clang::LangOptions&                        lang_opts,
                                     const std::set<std::string>&                     ns_drop_set,
                                     const std::map<const clang::Decl*, std::string>& expos_set) {
    AttachedItem attached;
    attached.inclass_offset = sm.getDecomposedLoc(decl->getBeginLoc()).second;
    attached.file           = sm.getFileID(decl->getBeginLoc());
    attach_docblock(attached, decl, sm);

    const VariableMask mask = variable_seebelow_mask(decl, attached.directives, attached.grouping_line);
    if (mask.diagnostic)
        attached.diagnostics.push_back(*mask.diagnostic);
    // A concept reaches this path too, and its definition masks the same way
    // an alias's RHS does on attach_alias's (issue #50); the marker used to be
    // accepted here with no effect and no diagnostic.
    std::optional<clang::SourceRange> see_below;
    if (attached.directives.seebelow && !attached.directives.seebelow_target)
        see_below = definition_mask_range(decl, sm, lang_opts);

    if (attached.verbatim_itemdecl)
        attached.item.decl.signatures.push_back(ir::CodeText{*attached.verbatim_itemdecl, {}});
    else
        attached.item.decl.signatures.push_back(extract_freestanding_declaration(decl,
                                                                                 sm,
                                                                                 lang_opts,
                                                                                 ns_drop_set,
                                                                                 expos_set,
                                                                                 /*exposition=*/false,
                                                                                 std::nullopt,
                                                                                 mask.unspecified,
                                                                                 see_below));
    attached.item.decl.entities.push_back(decl->getNameAsString());
    attached.item.decl.index.push_back({ir::IndexKind::Global, decl->getNameAsString(), {}});
    return attached;
}

// Build the AttachedItem for a documented static data member (issue #148): an
// itemdecl of its own and the elements its docblock carries, routed by its
// class's `\ref` group or an `\at`, the way an in-class alias is. The draft
// gives `atomic<T>::is_always_lock_free` and the `numeric_limits` members
// exactly this. `decl` is the VarDecl, or the VarTemplateDecl of a static data
// member template, whose extraction then starts at `template`. The initializer
// stays in the itemdecl unless bare `\seebelow` drops it; unlike a namespace
// variable's, the mask leaves the declared type alone, since a static data
// member's type is part of its interface. The class synopsis keeps the
// initializer either way, as [numeric.limits.general]'s does.
AttachedItem attach_static_member(const clang::NamedDecl*                          decl,
                                  const clang::CXXRecordDecl*                      record,
                                  const clang::SourceManager&                      sm,
                                  const clang::LangOptions&                        lang_opts,
                                  const std::set<std::string>&                     ns_drop_set,
                                  const std::map<const clang::Decl*, std::string>& expos_set) {
    AttachedItem attached;
    attached.inclass_offset = sm.getDecomposedLoc(decl->getBeginLoc()).second;
    attached.file           = sm.getFileID(decl->getBeginLoc());
    attach_docblock(attached, decl, sm);

    if (attached.directives.seebelow_target && attached.grouping_line > 0)
        attached.diagnostics.push_back({beman::specgen::Severity::Error,
                                        attached.grouping_line,
                                        "a static data member accepts only bare \\seebelow"});
    const clang::VarDecl* variable = llvm::dyn_cast<clang::VarDecl>(decl);
    if (const auto* var_tmpl = llvm::dyn_cast<clang::VarTemplateDecl>(decl))
        variable = var_tmpl->getTemplatedDecl();
    const bool drops_initializer = attached.directives.seebelow && !attached.directives.seebelow_target;

    if (attached.verbatim_itemdecl)
        attached.item.decl.signatures.push_back(ir::CodeText{*attached.verbatim_itemdecl, {}});
    else
        attached.item.decl.signatures.push_back(
            extract_freestanding_declaration(decl,
                                             sm,
                                             lang_opts,
                                             ns_drop_set,
                                             expos_set,
                                             /*exposition=*/false,
                                             std::nullopt,
                                             nullptr,
                                             std::nullopt,
                                             drops_initializer ? variable : nullptr));
    attached.item.decl.entities.push_back(decl->getNameAsString());
    attached.item.decl.index.push_back({ir::IndexKind::Member, decl->getNameAsString(), record->getNameAsString()});
    rewrite_prose_expos(attached.item.descr, expos_spellings(record, expos_set));
    return attached;
}

// --- Shared unwrapping projections ------------------------------------------
//
// collect_inclass_items below and the build_omit_set/build_expos_set/
// build_seebelow_map pre-passes further down each walk a class's members
// looking for a function or a hidden friend, or walk `decls` looking for an
// out-of-line function definition or a class/class-template definition. Their
// own comments already say so ("Same pre-pass shape as build_omit_set"). The
// projections below are that repeated dyn_cast unwrapping, named once each;
// the caller-specific predicate (does the docblock say \omit, \expos,
// \seebelow; does the member carry a real body) stays local to each caller,
// since that is the part that is not actually shared.

// The out-of-line function a top-level decl defines: itself if a
// FunctionDecl, or a FunctionTemplateDecl's templated declaration.
// build_expos_set has no out-of-line case of its own (exposition-only
// applies only to members), so only build_omit_set and build_seebelow_map
// use this.
const clang::FunctionDecl* as_out_of_line_function(const clang::Decl* decl) {
    if (const auto* fn = llvm::dyn_cast<clang::FunctionDecl>(decl))
        return fn;
    if (const auto* ft = llvm::dyn_cast<clang::FunctionTemplateDecl>(decl))
        return ft->getTemplatedDecl();
    return nullptr;
}

// A specialization and the primary it specializes, or {decl, nullptr} when
// `decl` specializes nothing. Partial and explicit alike: the partial kinds
// derive from these, so one cast each covers both (issue #49).
std::pair<const clang::Decl*, const clang::Decl*> specialized_primary(const clang::Decl* decl) {
    if (const auto* record = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(decl))
        return {decl, record->getSpecializedTemplate()};
    if (const auto* variable = llvm::dyn_cast<clang::VarTemplateSpecializationDecl>(decl))
        return {decl, variable->getSpecializedTemplate()};
    return {decl, nullptr};
}

// A class or class template's templated record, if `decl` names either.
// Shared by all three set-builder pre-passes' outer walk.
const clang::CXXRecordDecl* as_record_decl(const clang::Decl* decl) {
    if (const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(decl))
        return record;
    if (const auto* ct = llvm::dyn_cast<clang::ClassTemplateDecl>(decl))
        return ct->getTemplatedDecl();
    return nullptr;
}

// The function a member decl names without unwrapping a function template.
// build_seebelow_map uses this as its first projection before handling a plain
// FunctionTemplateDecl explicitly.
const clang::FunctionDecl* member_as_function_or_friend(const clang::Decl* member) {
    if (const auto* friend_decl = llvm::dyn_cast<clang::FriendDecl>(member))
        return llvm::dyn_cast_or_null<clang::FunctionDecl>(friend_decl->getFriendDecl());
    return llvm::dyn_cast<clang::FunctionDecl>(member);
}

// Build the SpecItem for a top-level decl that is not a class/class template
// (design §3.3, §6 "Public member, out-of-line def"). `decl` may be a plain
// FunctionDecl or a FunctionTemplateDecl (a member function template of a
// class, or of a class template, defined out-of-line arrives this way; a
// non-template member of a class template, like `box<T>::get()`, does not —
// it is a plain FunctionDecl whose lexical text happens to carry the class's
// template head).
//
// An out-of-line member definition is attached as before. A namespace-scope
// free-function definition is attached only when it carries a docblock: public
// wording is explicit, while unannotated implementation helpers remain absent.
// Both the semantic and lexical contexts must be file contexts. The lexical
// half excludes hidden friends, which are semantic namespace functions but
// belong to collect_inclass_items because their source declaration is in a
// class body. Ordinary in-class definitions are handled there too.
AttachedItem build_spec_item(clang::Decl*                                     decl,
                             const clang::SourceManager&                      sm,
                             const clang::LangOptions&                        lang_opts,
                             const std::set<std::string>&                     ns_drop_set,
                             const std::map<const clang::Decl*, std::string>& expos_set,
                             const SkippedRanges&                             skipped) {
    const clang::FunctionDecl* fn = llvm::dyn_cast<clang::FunctionDecl>(decl);
    if (fn == nullptr) {
        if (const auto* fn_tmpl = llvm::dyn_cast<clang::FunctionTemplateDecl>(decl))
            fn = fn_tmpl->getTemplatedDecl();
    }

    if (fn == nullptr || !fn->isThisDeclarationADefinition())
        return {};

    const auto* method             = llvm::dyn_cast<clang::CXXMethodDecl>(fn);
    const bool  out_of_line_member = method != nullptr && method->isOutOfLine();
    const bool  documented_free    = method == nullptr && fn->getDeclContext()->isFileContext() &&
                                     fn->getLexicalDeclContext()->isFileContext() && has_docblock(fn, sm);
    if (!out_of_line_member && !documented_free)
        return {};

    // Markup/body/derivations come from the definition; itemdecl text comes
    // from its first declaration (design §3.3). A free function stays ordered
    // where its documented definition appears, even when its first declaration
    // is earlier. Not a hidden friend, so there is no friend-begin override.
    AttachedItem attached =
        attach_function(fn, fn->getFirstDecl(), /*friend_begin=*/{}, sm, lang_opts, ns_drop_set, expos_set, skipped);
    if (documented_free)
        attached.inclass_offset = sm.getDecomposedLoc(decl->getBeginLoc()).second;
    attached.file = sm.getFileID(decl->getBeginLoc());
    return attached;
}

// --- in-class-member collection (design §3.3 sub-case, §6) ------------------

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
                return kv.first >= class_begin && kv.first < class_end &&
                       parse_ref(kv.second->getRawText(sm)).has_value();
            }) | std::views::transform([&](const auto& kv) {
                return std::pair<unsigned, std::string>{kv.first, *parse_ref(kv.second->getRawText(sm))};
            }) | std::ranges::to<std::vector<std::pair<unsigned, std::string>>>();
        }
    }

    // Stable name of the nearest `\ref` group at or before `offset`, else "".
    // groups_ is ascending, so the answer is the element just before the
    // partition point of "ref_offset <= offset" -- a binary search, not a
    // linear scan with a break.
    std::string section_for(unsigned offset) const {
        const auto it = std::ranges::partition_point(groups_, [&](const auto& rg) { return rg.first <= offset; });
        return it == groups_.begin() ? std::string{} : std::prev(it)->second;
    }

  private:
    std::vector<std::pair<unsigned, std::string>> groups_;
};

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
