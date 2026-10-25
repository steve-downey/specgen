// src/beman/specgen/frontend/header_synopsis.cpp                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/header_synopsis.hpp>

#include <beman/specgen/ir.hpp>
#include <beman/specgen/rsec.hpp>
#include <beman/specgen/section_markers.hpp>
#include <beman/specgen/span_sentinels.hpp>
#include <beman/specgen/synopsis_edits.hpp>
#include <beman/specgen/frontend/decl_projections.hpp>
#include <beman/specgen/frontend/draft_format.hpp>
#include <beman/specgen/frontend/expos_spellings.hpp>
#include <beman/specgen/frontend/file_views.hpp>
#include <beman/specgen/frontend/qualifiers.hpp>
#include <beman/specgen/frontend/synopsis_extraction.hpp>
#include <beman/specgen/frontend/classify.hpp>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RawCommentList.h>
#include <clang/AST/Stmt.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/TokenKinds.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Token.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

using grammar::rsec_tag_recognized;
using grammar::section_end_fence;
using span_sentinels::recover_sentinels;
using span_sentinels::span_sentinel;
using span_sentinels::SpanInfo;
using synopsis_edits::sort_back_to_front;
using synopsis_edits::SynopsisEdit;

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

beman::specgen::ir::CodeText extract_header_declaration(clang::Decl*                                     decl,
                                                        const clang::SourceManager&                      sm,
                                                        const clang::LangOptions&                        lang_opts,
                                                        const std::set<std::string>&                     ns_drop_set,
                                                        const std::map<const clang::Decl*, std::string>& expos_set,
                                                        const clang::VarDecl*                            unspecified,
                                                        std::optional<clang::SourceRange>                see_below) {
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

} // namespace beman::specgen::frontend
