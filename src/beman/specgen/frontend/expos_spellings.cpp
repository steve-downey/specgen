// src/beman/specgen/frontend/expos_spellings.cpp                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/expos_spellings.hpp>

#include <beman/specgen/frontend/record_members.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/section_markers.hpp>
#include <beman/specgen/span_sentinels.hpp>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Expr.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/RawCommentList.h>
#include <clang/AST/TypeLoc.h>
#include <llvm/Support/Casting.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace beman::specgen::frontend {

using grammar::parse_ref;
using span_sentinels::span_sentinel;
using span_sentinels::SpanInfo;

std::map<std::string, std::string> expos_spellings(const clang::CXXRecordDecl*                      record,
                                                   const std::map<const clang::Decl*, std::string>& expos_set) {
    if (record == nullptr)
        return {};
    auto expos_members = real_record_members(record) | std::views::filter([&](const RealRecordMember& member) {
                             const auto* member_fn = member_function_or_template(member.decl);
                             const auto* named = member_fn != nullptr ? llvm::dyn_cast<clang::NamedDecl>(member_fn)
                                                                      : llvm::dyn_cast<clang::NamedDecl>(member.decl);
                             return named != nullptr && named->getIdentifier() != nullptr &&
                                    expos_set.contains(named->getCanonicalDecl());
                         });

    std::map<std::string, std::string> spellings;
    // substrate generic algorithm: a scatter into a map keyed by data, the
    // same shape document_build.cpp's pending-bucket population is marked
    // for. Not `ranges::to<map>`: two overloads in one class can share a
    // written spelling, and assignment keeps the *last* of them, whereas
    // `ranges::to` selects map's from_range_t (insert) constructor and would
    // silently keep the first instead.
    for (const RealRecordMember& member : expos_members) {
        const auto* member_fn = member_function_or_template(member.decl);
        const auto* named =
            member_fn != nullptr ? llvm::cast<clang::NamedDecl>(member_fn) : llvm::cast<clang::NamedDecl>(member.decl);
        spellings[named->getName().str()] = expos_set.at(named->getCanonicalDecl());
    }
    return spellings;
}

bool is_ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

std::string apply_expos_sentinels(std::string                               text,
                                  const std::map<std::string, std::string>& spellings,
                                  unsigned&                                 span_n,
                                  std::map<std::string, SpanInfo>&          sentinels) {
    if (spellings.empty())
        return text;
    std::string out;
    out.reserve(text.size());
    std::size_t pos = 0;
    // substrate generic algorithm: a position-tracking left-to-right walk
    // that rewrites whole-identifier matches into sentinels is the
    // array-substitution primitive itself (the same shape backend/code_spans.hpp's
    // render_code_spans marks) -- there is no smaller unit to fold over.
    while (pos < text.size()) {
        if (!is_ident_char(text[pos]) || (pos > 0 && is_ident_char(text[pos - 1]))) {
            out += text[pos++];
            continue;
        }
        const std::size_t end = static_cast<std::size_t>(
            std::ranges::find_if_not(text.cbegin() + static_cast<std::ptrdiff_t>(pos), text.cend(), is_ident_char) -
            text.cbegin());
        const std::string word = text.substr(pos, end - pos);
        if (const auto it = spellings.find(word); it != spellings.end()) {
            const std::string sentinel = span_sentinel(span_n++);
            sentinels[sentinel]        = SpanInfo{beman::specgen::ir::SpanKind::ExposId, it->second, it->second};
            out += sentinel;
        } else {
            out += word;
        }
        pos = end;
    }
    return out;
}

std::string apply_ref_sentinels(std::string text, unsigned& span_n, std::map<std::string, SpanInfo>& sentinels) {
    namespace ir                    = beman::specgen::ir;
    constexpr std::string_view kRef = "\\ref{";

    std::string out;
    out.reserve(text.size());
    std::size_t pos = 0;
    // substrate generic algorithm: a position-tracking left-to-right walk that
    // rewrites matches into sentinels is the array-substitution primitive
    // itself -- the same shape apply_expos_sentinels above is marked for.
    while (true) {
        const std::size_t at = text.find(kRef, pos);
        if (at == std::string::npos)
            break;
        const std::size_t newline    = text.rfind('\n', at);
        const std::size_t line_begin = newline == std::string::npos ? 0 : newline + 1;
        const std::size_t line_end   = std::min(text.find('\n', at), text.size());
        const std::size_t comment    = text.find("//", line_begin);
        const std::size_t close      = text.find('}', at + kRef.size());
        if (comment == std::string::npos || comment > at || close == std::string::npos || close > line_end) {
            // Not a cross-reference we can name: leave the text alone and
            // resume past this occurrence rather than rescanning it.
            out += text.substr(pos, at + kRef.size() - pos);
            pos = at + kRef.size();
            continue;
        }

        const std::string name     = text.substr(at + kRef.size(), close - at - kRef.size());
        const std::string sentinel = span_sentinel(span_n++);
        // The display text is what the draft *renders* -- `\ref{x}` sets
        // "[x]" -- so a reader of CodeText::text alone, and any backend that
        // ignored spans, still sees a cross-reference rather than a macro.
        sentinels[sentinel] = SpanInfo{ir::SpanKind::Ref, "[" + name + "]", name};
        out += text.substr(pos, at - pos);
        out += sentinel;
        pos = close + 1;
    }
    out += text.substr(pos);
    return out;
}

void rewrite_prose_expos(beman::specgen::ir::ItemDescr& descr, const std::map<std::string, std::string>& spellings) {
    namespace ir = beman::specgen::ir;
    if (spellings.empty())
        return;
    // Each level rebuilds a fresh container via transform rather than
    // mutating the one it was handed -- a write at the current position is an
    // append. `ir::Inline`/`ir::Paragraph`/`ir::DescriptionElement` are all
    // cheap to move (variant-of-strings, vector-of-that), so the rebuild
    // costs nothing a reference mutation wouldn't already pay for the string
    // copies inside it.
    const auto fix_inline = [&](ir::Inline piece) {
        auto* code = std::get_if<ir::CodeInline>(&piece);
        if (code == nullptr || !code->code.spans.empty())
            return piece;
        const auto it = spellings.find(code->code.text);
        if (it == spellings.end())
            return piece;
        code->code.text  = it->second;
        code->code.spans = {ir::Span{0, code->code.text.size(), ir::SpanKind::ExposId, it->second}};
        return piece;
    };
    const auto fix_paragraph = [&](ir::Paragraph para) {
        return std::move(para) | std::views::as_rvalue | std::views::transform(fix_inline) |
               std::ranges::to<ir::Paragraph>();
    };
    // Shared by both arms below (prose paragraphs and itemize items) so the
    // walk over a `vector<Paragraph>` exists exactly once.
    const auto fix_paragraphs = [&](std::vector<ir::Paragraph> paras) {
        return std::move(paras) | std::views::as_rvalue | std::views::transform(fix_paragraph) |
               std::ranges::to<std::vector<ir::Paragraph>>();
    };
    const auto fix_element = [&](ir::DescriptionElement el) {
        el.paragraphs = fix_paragraphs(std::move(el.paragraphs));
        if (el.itemize)
            el.itemize->items = fix_paragraphs(std::move(el.itemize->items));
        if (el.table) {
            el.table->caption = fix_paragraph(std::move(el.table->caption));
            el.table->column1 = fix_paragraph(std::move(el.table->column1));
            el.table->column2 = fix_paragraph(std::move(el.table->column2));
            el.table->rows    = std::move(el.table->rows) | std::views::as_rvalue |
                                std::views::transform([&](ir::Table2DRow row) {
                                 row.header = fix_paragraph(std::move(row.header));
                                 row.cell1  = fix_paragraph(std::move(row.cell1));
                                 row.cell2  = fix_paragraph(std::move(row.cell2));
                                 return row;
                                }) |
                                std::ranges::to<std::vector<ir::Table2DRow>>();
        }
        if (el.flat_table) {
            el.flat_table->caption = fix_paragraph(std::move(el.flat_table->caption));
            el.flat_table->column1 = fix_paragraph(std::move(el.flat_table->column1));
            el.flat_table->column2 = fix_paragraph(std::move(el.flat_table->column2));
            el.flat_table->rows    = std::move(el.flat_table->rows) | std::views::as_rvalue |
                                     std::views::transform([&](ir::Table1DRow row) {
                                      row.cell1 = fix_paragraph(std::move(row.cell1));
                                      row.cell2 = fix_paragraph(std::move(row.cell2));
                                      return row;
                                     }) |
                                     std::ranges::to<std::vector<ir::Table1DRow>>();
        }
        return el;
    };
    descr.elements = std::move(descr.elements) | std::views::as_rvalue | std::views::transform(fix_element) |
                     std::ranges::to<std::vector<ir::DescriptionElement>>();
}

} // namespace beman::specgen::frontend
