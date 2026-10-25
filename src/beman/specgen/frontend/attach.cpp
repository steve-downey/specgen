// src/beman/specgen/frontend/attach.cpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/attach.hpp>

#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/docblock.hpp>
#include <beman/specgen/document_build.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/lower.hpp>
#include <beman/specgen/span_sentinels.hpp>
#include <beman/specgen/synopsis_edits.hpp>
#include <beman/specgen/frontend/constraints.hpp>
#include <beman/specgen/frontend/declaration_masks.hpp>
#include <beman/specgen/frontend/docblock_lookup.hpp>
#include <beman/specgen/frontend/draft_format.hpp>
#include <beman/specgen/frontend/expos_spellings.hpp>
#include <beman/specgen/frontend/file_views.hpp>
#include <beman/specgen/frontend/itemdecl.hpp>
#include <beman/specgen/frontend/mandates.hpp>
#include <beman/specgen/frontend/qualifiers.hpp>
#include <beman/specgen/frontend/record_members.hpp>
#include <beman/specgen/frontend/synopsis_extraction.hpp>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclFriend.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
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
#include <expected>
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
using synopsis_edits::sort_back_to_front;
using synopsis_edits::SynopsisEdit;

namespace {

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

} // namespace

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

AttachedItem attach_alias(const clang::TypeAliasDecl*                      alias,
                          const clang::SourceManager&                      sm,
                          const clang::LangOptions&                        lang_opts,
                          const std::set<std::string>&                     ns_drop_set,
                          const std::map<const clang::Decl*, std::string>& expos_set,
                          const clang::TypeAliasTemplateDecl*              head) {
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

} // namespace beman::specgen::frontend
