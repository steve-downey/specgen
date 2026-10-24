// include/beman/specgen/docblock.hpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — docblock markup grammar. See docs/architecture.md §4.
// Sparse, unordered element tags mirroring the std description macros, plus
// structural markers. Pure string parser: no clang, no reference resolution —
// backtick spans carry raw names for the front end to resolve later.

#ifndef BEMAN_SPECGEN_DOCBLOCK_HPP
#define BEMAN_SPECGEN_DOCBLOCK_HPP

#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/markers.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace beman::specgen::grammar {

// The one severity vocabulary (diagnostic.hpp), shared with the Phase-4
// validators; kept as a same-spelled alias so every existing
// `Severity::Error` use site in this file and its callers compiles
// unchanged.
using Severity = beman::specgen::Severity;

struct Diagnostic {
    Severity    severity = Severity::Error;
    int         line     = 0; // 1-based, within the docblock
    std::string message;
};

struct InlineText {
    std::string text;
};
struct InlineCode {
    std::string code;
}; // backticked; unresolved
struct InlineRef {
    std::string stable_name;
}; // \iref{stable-name}; prose references may name an external subclause
using ProseInline    = std::variant<InlineText, InlineCode, InlineRef>;
using ProseParagraph = std::vector<ProseInline>;

struct Table2DRow {
    ProseParagraph header;
    ProseParagraph cell1;
    ProseParagraph cell2;
};

struct Table2D {
    std::string             stable_name;
    ProseParagraph          caption = {};
    ProseParagraph          column1 = {};
    ProseParagraph          column2 = {};
    std::vector<Table2DRow> rows    = {};
};

// A draft flat two-column table (\libtab2): one row per entry, no row-heading
// column. This is what an enumeration's meanings table needs (issue #74) --
// \lib2dtab2 is a cross-product table (a row-heading column plus two data
// columns) and has nothing narrower to offer.
struct Table1DRow {
    ProseParagraph cell1;
    ProseParagraph cell2;
};

struct Table1D {
    std::string             stable_name;
    ProseParagraph          caption = {};
    ProseParagraph          column1 = {};
    ProseParagraph          column2 = {};
    std::vector<Table1DRow> rows    = {};
};

struct Element {
    ir::ElementKind             kind       = ir::ElementKind::Effects;
    std::vector<ProseParagraph> paragraphs = {};
    std::vector<ProseParagraph> items      = {}; // authored \item entries, in source order
    std::optional<Table2D>      table      = {};
    std::optional<Table1D>      flat_table = {};
    int                         line       = 0; // line of the tag
};

struct Docblock {
    std::vector<Element>       elements;
    Markers                    markers;
    std::optional<std::string> verbatim_synopsis;
    // Terminal exact-text override for one itemdecl code block. Multiple
    // declarations remain one payload; the parser does not guess C++ boundaries.
    std::optional<std::string> verbatim_itemdecl;
};

struct ParseResult {
    Docblock                block;
    std::vector<Diagnostic> diags;

    bool ok() const {
        return std::ranges::none_of(diags, [](const auto& d) { return d.severity == Severity::Error; });
    }
};

// Strip `//!` line decorations or a `/*! ... */` block decoration (with
// optional leading `*` per line). Lines without decoration pass through.
std::string strip_comment_decorations(std::string_view raw);

// Parse a markup block (decorated or already stripped).
ParseResult parse_docblock(std::string_view text);

} // namespace beman::specgen::grammar

#endif // BEMAN_SPECGEN_DOCBLOCK_HPP
