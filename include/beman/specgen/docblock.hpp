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
    ProseParagraph          caption;
    ProseParagraph          column1;
    ProseParagraph          column2;
    std::vector<Table2DRow> rows;
};

struct Element {
    ir::ElementKind             kind = ir::ElementKind::Effects;
    std::vector<ProseParagraph> paragraphs;
    std::vector<ProseParagraph> items; // authored \item entries, in source order
    std::optional<Table2D>      table;
    int                         line = 0; // line of the tag
};

// The marker registry (design §4; decision marker-registry): one row per marker
// spelling, driving `find_marker` and the marker-vs-marker duplicate checks
// in parse_docblock. Adding a marker means adding a field to `Markers`
// (markers.hpp) plus one row here — never a second table or an ad-hoc `if`
// chain. `flag` covers the plain-boolean markers (most of them); markers
// that carry argument text instead (or in addition) — string-valued or
// arity-1 markers like `\expos(name)` and `\at` — carry a `set_arg` setter
// rather than being forced into `bool Markers::*`.
enum class MarkerArity {
    Flag,          // \merge          — presence only, no argument text.
    ParenOptional, // \expos          — an optional "(text)" argument right after the tag.
    RestOptional,  // \seebelow/\also — optional trailing text after the tag.
    RestRequired,  // \at             — required trailing text; carries no separate flag.
};

namespace detail {
// Setters for the markers whose value is text, not mere presence. Each
// returns whether it replaced an already-present value: the generic parser
// uses that to raise "duplicate marker" for markers with no dedicated `flag`
// to check instead (arity RestRequired, e.g. \at and \group). Defined in
// docblock.cpp; declared here only so `kMarkers` below can take their
// address.
bool set_expos_name(Markers& markers, std::string arg);
bool set_at_anchor(Markers& markers, std::string arg);
bool set_seebelow_target(Markers& markers, std::string arg);
bool set_group_id(Markers& markers, std::string arg);
bool set_also_target(Markers& markers, std::string arg);
} // namespace detail

struct MarkerInfo {
    std::string_view spelling;
    bool Markers::* flag                   = nullptr; // nullptr when the marker has no dedicated flag (\at).
    bool (*set_arg)(Markers&, std::string) = nullptr; // nullptr when the marker takes no argument text.
    MarkerArity      arity                 = MarkerArity::Flag;
    std::string_view missing_arg_error; // used only when arity == RestRequired and the text is empty.
};

// clang-format off
inline constexpr MarkerInfo kMarkers[] = {
    {.spelling = "expos",
     .flag     = &Markers::expos,
     .set_arg  = &detail::set_expos_name,
     .arity    = MarkerArity::ParenOptional},
    {.spelling = "merge",               .flag = &Markers::merge},
    {.spelling = "omit",                .flag = &Markers::omit},
    {.spelling = "describe",            .flag = &Markers::describe},
    {.spelling = "also",
     .flag     = &Markers::also,
     .set_arg  = &detail::set_also_target,
     .arity    = MarkerArity::RestOptional},
    {.spelling = "group",
     .set_arg  = &detail::set_group_id,
     .arity    = MarkerArity::RestRequired,
     .missing_arg_error = "\\group requires an id"},
    {.spelling = "effects-equiv",       .flag = &Markers::effects_equiv},
    {.spelling = "returns-equiv",       .flag = &Markers::returns_equiv},
    {.spelling = "constraints-in-decl", .flag = &Markers::constraints_in_decl},
    {.spelling = "freestanding",         .flag = &Markers::freestanding},
    {.spelling = "freestanding-deleted", .flag = &Markers::freestanding_deleted},
    {.spelling = "verbatim-synopsis",    .flag = &Markers::verbatim_synopsis},
    {.spelling = "verbatim-itemdecl"},
    {.spelling = "at",
     .set_arg  = &detail::set_at_anchor,
     .arity    = MarkerArity::RestRequired,
     .missing_arg_error = "\\at requires an anchor"},
    {.spelling = "seebelow",
     .flag     = &Markers::seebelow,
     .set_arg  = &detail::set_seebelow_target,
     .arity    = MarkerArity::RestOptional},
    {.spelling = "impdef",              .flag = &Markers::impdef},
};
// clang-format on

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
