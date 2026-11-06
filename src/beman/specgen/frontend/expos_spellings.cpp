// src/beman/specgen/frontend/expos_spellings.cpp                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/expos_spellings.hpp>

#include <beman/specgen/frontend/record_members.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/foundation/overloaded.hpp>
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
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace beman::specgen::frontend {

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

namespace {
namespace ir = beman::specgen::ir;

template <class Block>
struct ProseLeafRewriter {
    const std::map<std::string, std::string>& spellings;
    ir::Paragraph                             paragraph(ir::Paragraph) const;
    // A paragraph, list, and both table shapes carry semantic prose inlines.
    Block operator()(ir::BodyParagraph) const;
    Block operator()(ir::Itemize) const;
    Block operator()(ir::Table2D) const;
    Block operator()(ir::Table1D) const;
    // Authored code is verbatim and has no attached AST.
    Block operator()(ir::AuthoredCode) const;
};

template <class Block>
ir::Paragraph ProseLeafRewriter<Block>::paragraph(ir::Paragraph p) const {
    return std::move(p) | std::views::as_rvalue | std::views::transform([this](ir::Inline piece) {
               auto* code = std::get_if<ir::CodeInline>(&piece);
               if (code != nullptr && code->code.spans.empty()) {
                   const auto it = spellings.find(code->code.text);
                   if (it != spellings.end()) {
                       code->code.text  = it->second;
                       code->code.spans = {ir::Span{0, code->code.text.size(), ir::SpanKind::ExposId, it->second}};
                   }
               }
               return piece;
           }) |
           std::ranges::to<ir::Paragraph>();
}

template <class Block>
Block ProseLeafRewriter<Block>::operator()(ir::BodyParagraph p) const {
    p.text = paragraph(std::move(p.text));
    return p;
}

template <class Block>
Block ProseLeafRewriter<Block>::operator()(ir::Itemize list) const {
    list.items = std::move(list.items) | std::views::as_rvalue |
                 std::views::transform([this](ir::Paragraph p) { return paragraph(std::move(p)); }) |
                 std::ranges::to<std::vector>();
    return list;
}

template <class Block>
Block ProseLeafRewriter<Block>::operator()(ir::Table2D table) const {
    table.caption = paragraph(std::move(table.caption));
    table.column1 = paragraph(std::move(table.column1));
    table.column2 = paragraph(std::move(table.column2));
    table.rows    = std::move(table.rows) | std::views::as_rvalue | std::views::transform([this](ir::Table2DRow row) {
                     row.header = paragraph(std::move(row.header));
                     row.cell1  = paragraph(std::move(row.cell1));
                     row.cell2  = paragraph(std::move(row.cell2));
                     return row;
                    }) |
                    std::ranges::to<std::vector>();
    return table;
}

template <class Block>
Block ProseLeafRewriter<Block>::operator()(ir::Table1D table) const {
    table.caption = paragraph(std::move(table.caption));
    table.column1 = paragraph(std::move(table.column1));
    table.column2 = paragraph(std::move(table.column2));
    table.rows    = std::move(table.rows) | std::views::as_rvalue | std::views::transform([this](ir::Table1DRow row) {
                     row.cell1 = paragraph(std::move(row.cell1));
                     row.cell2 = paragraph(std::move(row.cell2));
                     return row;
                    }) |
                    std::ranges::to<std::vector>();
    return table;
}

template <class Block>
Block ProseLeafRewriter<Block>::operator()(ir::AuthoredCode code) const {
    return code;
}

ir::InformativeBody rewrite_informative(ir::InformativeBody, const std::map<std::string, std::string>&);
ir::InformativeBody rewrite_informative(ir::InformativeBody                       body,
                                        const std::map<std::string, std::string>& spellings) {
    return std::move(body) | std::views::as_rvalue | std::views::transform([&](ir::InformativeBlock block) {
               return std::visit(foundation::overloaded{ProseLeafRewriter<ir::InformativeBlock>{spellings}},
                                 std::move(block));
           }) |
           std::ranges::to<ir::InformativeBody>();
}

struct ProseExposRewriter : ProseLeafRewriter<ir::BodyBlock> {
    using ProseLeafRewriter<ir::BodyBlock>::operator();
    // Extracted code has already been resolved against its AST.
    ir::BodyBlock operator()(ir::EquivalentTo) const;
    ir::BodyBlock operator()(ir::Note) const;
    ir::BodyBlock operator()(ir::Example) const;
};

ir::BodyBlock ProseExposRewriter::operator()(ir::EquivalentTo code) const { return code; }
ir::BodyBlock ProseExposRewriter::operator()(ir::Note note) const {
    note.body = rewrite_informative(std::move(note.body), spellings);
    return note;
}
ir::BodyBlock ProseExposRewriter::operator()(ir::Example example) const {
    example.body = rewrite_informative(std::move(example.body), spellings);
    return example;
}
} // namespace

void rewrite_prose_expos(ir::ItemDescr& descr, const std::map<std::string, std::string>& spellings) {
    if (spellings.empty())
        return;
    const ProseExposRewriter rewrite{{spellings}};
    descr.leading = std::move(descr.leading) | std::views::as_rvalue | std::views::transform([&](ir::BodyBlock b) {
                        return std::visit(foundation::overloaded{rewrite}, std::move(b));
                    }) |
                    std::ranges::to<ir::DescriptionBody>();
    descr.elements =
        std::move(descr.elements) | std::views::as_rvalue | std::views::transform([&](ir::DescriptionElement e) {
            e.body = std::move(e.body) | std::views::as_rvalue | std::views::transform([&](ir::BodyBlock b) {
                         return std::visit(foundation::overloaded{rewrite}, std::move(b));
                     }) |
                     std::ranges::to<ir::DescriptionBody>();
            return e;
        }) |
        std::ranges::to<std::vector>();
}

} // namespace beman::specgen::frontend
