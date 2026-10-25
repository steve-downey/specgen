// src/beman/specgen/frontend/draft_format.cpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/draft_format.hpp>

#include <beman/specgen/ir.hpp>
#include <beman/specgen/span_sentinels.hpp>

#include <clang/Format/Format.h>
#include <clang/Tooling/Core/Replacement.h>
#include <llvm/Support/Error.h>

#include <algorithm>
#include <cstddef>
#include <format>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

namespace {

using span_sentinels::recover_sentinels;
using span_sentinels::resize_sentinels;
using span_sentinels::SpanInfo;

// Draft-tuned FormatStyle (design §3.6 step 2): start from the LLVM style and
// layer on the options that make the draft's template/requires/concept
// conventions come out right — templates get their own line, requires-clauses
// go on their own (indented) line rather than trailing the declaration, and
// concepts break before their body. The column limit and return-type penalty
// favor the tighter, name-and-type-together shape draft synopses use. Per
// design §3.6, tuned via golden-file diff rather than derived a priori.
clang::format::FormatStyle draft_format_style() {
    clang::format::FormatStyle style     = clang::format::getLLVMStyle();
    style.BreakTemplateDeclarations      = clang::format::FormatStyle::BTDS_MultiLine;
    style.SpaceAfterTemplateKeyword      = false;
    style.RequiresClausePosition         = clang::format::FormatStyle::RCPS_OwnLine;
    style.IndentRequiresClause           = true;
    style.BreakBeforeConceptDeclarations = clang::format::FormatStyle::BBCDS_Always;
    style.ColumnLimit                    = kDraftColumnLimit;
    style.PenaltyReturnTypeOnItsOwnLine  = 1000; // keep return type and name together
    // WG21 draft wording attaches references/pointers to the type (`const T&`,
    // `T*`), not the declarator; getLLVMStyle() defaults to the opposite.
    style.PointerAlignment = clang::format::FormatStyle::PAS_Left;
    // And it writes the qualifier on the left, always: `const T&`, never
    // `T const&`. Left unset, clang-format keeps whichever order the header
    // used, so the draft's spelling depended on the library's house style
    // (issue #39). A header may write either; the wording says `const T&`.
    //
    // QualifierOrder is set alongside, and has to be: clang-format derives it
    // from QualifierAlignment only while *parsing YAML*, so a style built in
    // code gets the enum and an empty order, and the fixer it gates then has
    // nothing to reorder by and silently does nothing.
    style.QualifierAlignment            = clang::format::FormatStyle::QAS_Left;
    style.QualifierOrder                = {"const", "volatile", "type"};
    style.KeepEmptyLines.AtStartOfBlock = false;
    return style;
}

// Run clang-format over `code` and apply its replacements, the same
// apply-or-fall-back shape smoke_check uses for its proof-of-life formatting
// call: if applyAllReplacements fails, consume the error and hand back the
// input unchanged rather than propagating a formatting failure into the IR.
std::string format_code(std::string_view code, const clang::format::FormatStyle& style) {
    const std::string                        text   = std::string(code);
    const std::vector<clang::tooling::Range> ranges = {clang::tooling::Range(0, static_cast<unsigned>(text.size()))};
    const clang::tooling::Replacements       replacements = clang::format::reformat(style, text, ranges);
    if (llvm::Expected<std::string> formatted = clang::tooling::applyAllReplacements(text, replacements))
        return *formatted;
    else
        llvm::consumeError(formatted.takeError());
    return text;
}

// Whether some line of `text` holds nothing but a `;`.
bool has_lone_semicolon(std::string_view text) {
    return std::ranges::any_of(std::views::split(text, '\n'), [](auto line) {
        const std::string_view view(line.begin(), line.end());
        const std::size_t      first = view.find_first_not_of(" \t");
        return first != std::string_view::npos && view.substr(first) == ";";
    });
}

// The draft's qualifier order applied to a fragment whose layout is already
// what the wording wants. Expression text -- a derived conjunct, an
// *Equivalent to:* body -- is taken from source verbatim and never reflowed,
// so a header writing `T const&` put `T const&` in a Constraints element while
// the itemdecl above it, which does go through clang-format, said `const T&`
// (issue #39). ColumnLimit 0 keeps the reflow off while the qualifier fixer
// runs: with no limit clang-format honours the input's own line breaks.
clang::format::FormatStyle qualifier_style() {
    clang::format::FormatStyle style = draft_format_style();
    style.ColumnLimit                = 0;
    return style;
}

constexpr std::string_view expr_context_prefix = "auto beman_specgen_expr = ";

} // namespace

std::string format_draft(std::string_view code, unsigned indent) {
    clang::format::FormatStyle style = draft_format_style();
    style.ColumnLimit -= indent;
    const std::string formatted = format_code(code, style);
    if (!has_lone_semicolon(formatted))
        return formatted;
    const unsigned limit = style.ColumnLimit;
    // substrate generic algorithm: a bounded retry whose every attempt is a
    // full reformat; the first acceptable one ends it.
    for (unsigned narrower = 1; narrower <= 8; ++narrower) {
        style.ColumnLimit = limit - narrower;
        std::string again = format_code(code, style);
        if (!has_lone_semicolon(again))
            return again;
    }
    return formatted;
}

ir::CodeText format_and_recover(std::string                            text,
                                const std::map<std::string, SpanInfo>& sentinels,
                                std::optional<std::string_view>        record_tag,
                                unsigned                               indent) {
    auto [sized_text, sized] = resize_sentinels(std::move(text), sentinels);
    std::string formatted    = format_draft(sized_text, indent);

    // BTDS_MultiLine matches the draft for function templates, but considers a
    // record head short even when its body is not and joins `template<...>` to
    // `class`/`struct`. The AST gives this path the record tag, so preserve
    // that one draft line break without guessing at arbitrary formatted code.
    if (record_tag && !record_tag->empty() && formatted.starts_with("template<")) {
        const std::size_t line_end = formatted.find('\n');
        const std::size_t pos      = formatted.rfind(std::format(" {} ", *record_tag), line_end);
        if (pos != std::string::npos && pos < line_end)
            formatted[pos] = '\n';
    }
    return recover_sentinels(std::move(formatted), sized);
}

std::string format_expr_fragment(const std::string& text) {
    std::string formatted = format_code(std::format("{}{};", expr_context_prefix, text), qualifier_style());
    // Should the wrapper not come back intact, format the fragment on its own
    // rather than guess at where it now starts and ends: format_code already
    // degrades to its input when clang-format fails, and this keeps that the
    // only failure mode.
    if (!formatted.starts_with(expr_context_prefix) || !formatted.ends_with(';'))
        return format_code(text, qualifier_style());
    formatted.pop_back();
    formatted.erase(0, expr_context_prefix.size());
    return formatted;
}

} // namespace beman::specgen::frontend
