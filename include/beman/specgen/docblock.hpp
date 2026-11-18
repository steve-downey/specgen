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
    bool        bare = false;
}; // \iref{stable-name}, and \ref{stable-name} (bare); prose references may name an external subclause
struct InlinePlaceholder {
    std::string text;
    bool        cv_qualifier = false;
}; // \placeholder{x}, \cv{} (text "cv"), and \cvqual{x} (cv_qualifier)
struct InlineOldConcept {
    std::string name;
}; // \oldconcept{X}
struct InlineConcept {
    std::string name;
    bool        exposition = false;
}; // \libconcept{c}, and \exposconcept{c} (exposition)
struct InlineTableRef {
    std::string stable_name;
}; // \tref{stable-name}
using ProseInline = std::
    variant<InlineText, InlineCode, InlineRef, InlinePlaceholder, InlineOldConcept, InlineConcept, InlineTableRef>;
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
    // Set where the block opens a numbered paragraph of its own: two blank
    // decorated lines before it (ir::opens_paragraph).
    bool opens_paragraph = false;
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
    // Set where the block opens a numbered paragraph of its own: two blank
    // decorated lines before it (ir::opens_paragraph).
    bool opens_paragraph = false;
};

struct BodyParagraph {
    ProseParagraph text;
    bool           continuation = false;
};

struct Itemize {
    std::vector<ProseParagraph> items;
    // Set where the block opens a numbered paragraph of its own: two blank
    // decorated lines before it (ir::opens_paragraph).
    bool opens_paragraph = false;
};

struct AuthoredCode {
    ir::CodeText code;
    // Set where the block opens a numbered paragraph of its own: two blank
    // decorated lines before it (ir::opens_paragraph).
    bool opens_paragraph = false;
};
using InformativeBlock = std::variant<BodyParagraph, Itemize, Table2D, Table1D, AuthoredCode>;
using InformativeBody  = std::vector<InformativeBlock>;
struct Note {
    InformativeBody body;
    // Set where the block opens a numbered paragraph of its own: two blank
    // decorated lines before it (ir::opens_paragraph).
    bool opens_paragraph = false;
};
struct Example {
    InformativeBody body;
    // Set where the block opens a numbered paragraph of its own: two blank
    // decorated lines before it (ir::opens_paragraph).
    bool opens_paragraph = false;
};
using BodyBlock       = std::variant<BodyParagraph, Itemize, Table2D, Table1D, AuthoredCode, Note, Example>;
using DescriptionBody = std::vector<BodyBlock>;

struct Element {
    ir::ElementKind             kind = ir::ElementKind::Effects;
    DescriptionBody             body;
    int                         line     = 0;
    std::vector<ProseParagraph> coverage = {};
};

// The finding for `\covers <element> P` in a docblock without `\asdraft`.
// Named because one reader takes it back: on a class definition the scoped
// form has a second meaning that needs no `\asdraft` (issue #283, the class's
// leading prose standing for its derived paragraph), and the grammar cannot
// see what a docblock is attached to.
inline constexpr std::string_view kCoversNeedsAsdraft =
    "\\covers naming an element applies to inherited wording, so it requires \\asdraft";

// `\covers constraints P`: coverage claimed for an element the docblock does
// not author, which `\asdraft` inherits.
struct KindCover {
    ir::ElementKind kind = ir::ElementKind::Constraints;
    ProseParagraph  claim;
    int             line = 0;
};

struct Docblock {
    std::vector<KindCover>     kind_covers;
    std::vector<Element>       elements;
    DescriptionBody            leading = {};
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
