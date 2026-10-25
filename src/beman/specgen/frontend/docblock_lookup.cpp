// src/beman/specgen/frontend/docblock_lookup.cpp                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/docblock_lookup.hpp>

#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/docblock.hpp>
#include <beman/specgen/document_build.hpp>
#include <beman/specgen/frontend/file_views.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/lower.hpp>

#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/RawCommentList.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <llvm/ADT/StringRef.h>

#include <cstddef>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

namespace {

// Byte offset within `raw` (a RawComment::getRawText() string, decoration
// included) of the first line whose vocabulary `accept` takes, or nullopt if
// no line does.
//
// Clang merges consecutive `//` line comments into a single RawComment, so the
// Beman shape
//
//     // \ref{optional.dtor}, destructor
//     //! \merge
//     constexpr ~optional() requires ... = default;
//
// arrives as ONE comment whose first line is a draft-form `\ref` header and
// whose markup starts partway in. Returning the offset lets callers keep the
// `\ref` line in the synopsis while still parsing (and stripping) the `//!`
// part — checking only the first line would miss the markup entirely.
template <typename Accept>
std::optional<std::size_t> vocabulary_start(llvm::StringRef raw, Accept accept) {
    std::size_t line_begin = 0;
    // substrate generic algorithm: the function's own contract is a byte
    // offset into raw, so each line's starting offset must survive scanning
    // it -- views::split('\n') would hand back substrings and throw that
    // offset away, forcing the same bookkeeping back in through a side
    // channel.
    while (line_begin <= raw.size()) {
        std::size_t pos = raw.find_first_not_of(" \t", line_begin);
        if (pos == llvm::StringRef::npos)
            pos = raw.size();
        if (accept(line_vocabulary(raw.substr(pos))))
            return line_begin;
        const std::size_t newline = raw.find('\n', line_begin);
        if (newline == llvm::StringRef::npos)
            break;
        line_begin = newline + 1;
    }
    return std::nullopt;
}

} // namespace

CommentVocabulary line_vocabulary(llvm::StringRef rest) {
    if (rest.starts_with("//!") || rest.starts_with("/*!"))
        return CommentVocabulary::Markup;
    if (rest.starts_with("///") || rest.starts_with("/**"))
        return CommentVocabulary::Doxygen;
    return CommentVocabulary::Draft;
}

std::optional<std::size_t> docblock_start(llvm::StringRef raw) {
    return vocabulary_start(raw, [](CommentVocabulary v) { return v == CommentVocabulary::Markup; });
}

std::optional<std::size_t> stripped_comment_start(llvm::StringRef raw) {
    return vocabulary_start(raw, [](CommentVocabulary v) { return v != CommentVocabulary::Draft; });
}

bool is_docblock_comment(llvm::StringRef raw) { return docblock_start(raw).has_value(); }

std::vector<beman::specgen::document_build::Diagnostic>
docblock_diagnostics(const clang::RawComment*                rc,
                     std::size_t                             markup_start,
                     const std::vector<grammar::Diagnostic>& diags,
                     const clang::SourceManager&             sm) {
    if (diags.empty())
        return {};
    const unsigned first_line =
        sm.getSpellingLineNumber(rc->getBeginLoc().getLocWithOffset(static_cast<int>(markup_start)));
    const std::string file = diagnostic_file_name(sm, sm.getFileID(rc->getBeginLoc()));
    return diags | std::views::transform([&](const grammar::Diagnostic& d) {
               const unsigned line = d.line > 0 ? first_line + static_cast<unsigned>(d.line) - 1 : first_line;
               return beman::specgen::document_build::Diagnostic{d.severity, line, d.message, file};
           }) |
           std::ranges::to<std::vector<beman::specgen::document_build::Diagnostic>>();
}

const clang::RawComment* attached_raw_comment(const clang::Decl* decl) {
    const clang::TemplateDecl* described = decl->getDescribedTemplate();
    return decl->getASTContext().getRawCommentNoCache(described != nullptr ? described : decl);
}

bool has_docblock(const clang::Decl* decl, const clang::SourceManager& sm) {
    const clang::RawComment* rc = attached_raw_comment(decl);
    return rc != nullptr && is_docblock_comment(rc->getRawText(sm));
}

beman::specgen::lowering::ItemDirectives docblock_directives(const clang::Decl* decl, const clang::SourceManager& sm) {
    if (const clang::RawComment* rc = attached_raw_comment(decl)) {
        const llvm::StringRef raw = rc->getRawText(sm);
        if (const std::optional<std::size_t> start = docblock_start(raw))
            return beman::specgen::lowering::lower(
                       beman::specgen::grammar::parse_docblock(raw.substr(*start).str()).block)
                .directives;
    }
    return {};
}

bool suppresses_generated_entity(const beman::specgen::lowering::ItemDirectives& directives) {
    return directives.omit || directives.merge || directives.elsewhere;
}

std::optional<std::vector<beman::specgen::document_build::Diagnostic>>
record_suppression_diagnostics(const clang::Decl* decl, const clang::SourceManager& sm) {
    const clang::RawComment* rc = attached_raw_comment(decl);
    if (rc == nullptr)
        return std::nullopt;
    const llvm::StringRef            raw   = rc->getRawText(sm);
    const std::optional<std::size_t> start = docblock_start(raw);
    if (!start)
        return std::nullopt;

    const grammar::ParseResult parsed  = grammar::parse_docblock(raw.substr(*start).str());
    const lowering::Lowered    lowered = lowering::lower(parsed.block);
    if (!suppresses_generated_entity(lowered.directives))
        return std::nullopt;
    return docblock_diagnostics(rc, *start, parsed.diags, sm);
}

RecordDocblock record_docblock(const clang::Decl* decl, const clang::SourceManager& sm) {
    const clang::RawComment* rc = attached_raw_comment(decl);
    if (rc == nullptr)
        return {};
    const llvm::StringRef            raw   = rc->getRawText(sm);
    const std::optional<std::size_t> start = docblock_start(raw);
    if (!start)
        return {};

    const grammar::ParseResult parsed = grammar::parse_docblock(raw.substr(*start).str());

    RecordDocblock out;
    out.diagnostics = docblock_diagnostics(rc, *start, parsed.diags, sm);
    out.descr       = lowering::lower(parsed.block).descr;

    const auto reject = [&](std::string message) {
        const std::vector<grammar::Diagnostic> invalid{{beman::specgen::Severity::Error, 0, std::move(message)}};
        out.diagnostics.append_range(docblock_diagnostics(rc, *start, invalid, sm));
    };

    if (parsed.block.verbatim_itemdecl)
        reject("\\verbatim-itemdecl applies to a declaration; a class definition takes \\verbatim-synopsis");
    // An extraction marker (design §4.2) reads a *function definition's* body.
    // A class has none, so lowering's placeholder EquivalentTo is never filled
    // (`\returns-expr` leaves none, but is just as misplaced)
    // and its element would render as an "Equivalent to:" with an empty code
    // block under it -- design §9's blank box, one node over. Report it and
    // drop the placeholder, rather than print the box the Error is about.
    if (parsed.block.markers.effects_equiv || parsed.block.markers.returns_expr) {
        reject("an extraction marker applies to a function definition, not to a class definition");
        std::erase_if(out.descr.elements, [](const beman::specgen::ir::DescriptionElement& element) {
            return element.equivalent && element.equivalent->code.text.empty();
        });
    }
    if (parsed.block.markers.verbatim_synopsis)
        out.synopsis = parsed.block.verbatim_synopsis.value_or("");
    return out;
}

} // namespace beman::specgen::frontend
