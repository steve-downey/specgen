// src/beman/specgen/frontend/constraints.cpp                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/constraints.hpp>

#include <beman/specgen/conjuncts.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/span_sentinels.hpp>
#include <beman/specgen/synopsis_edits.hpp>
#include <beman/specgen/frontend/draft_format.hpp>
#include <beman/specgen/frontend/expos_spellings.hpp>
#include <beman/specgen/frontend/merged_twins.hpp>
#include <beman/specgen/frontend/qualifiers.hpp>
#include <beman/specgen/frontend/requires_clauses.hpp>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprConcepts.h>
#include <clang/AST/OperationKinds.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/TypeBase.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Token.h>
#include <llvm/Support/Casting.h>

#include <algorithm>
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

using span_sentinels::recover_sentinels;
using span_sentinels::span_sentinel;
using span_sentinels::SpanInfo;
using synopsis_edits::add_dominant_edit;
using synopsis_edits::sort_back_to_front;
using synopsis_edits::SynopsisEdit;

namespace {

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

} // namespace

beman::specgen::ir::CodeText expr_code_rewritten(const clang::Expr*                               e,
                                                 const clang::SourceManager&                      sm,
                                                 const clang::LangOptions&                        lang_opts,
                                                 const std::set<std::string>&                     ns_drop_set,
                                                 const std::map<const clang::Decl*, std::string>& expos_set,
                                                 const std::map<std::string, std::string>&        expos_names) {
    return code_rewritten(e,
                          clang::CharSourceRange::getTokenRange(e->getSourceRange()),
                          sm,
                          lang_opts,
                          ns_drop_set,
                          expos_set,
                          expos_names);
}

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

} // namespace beman::specgen::frontend
