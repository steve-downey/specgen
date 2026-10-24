// include/beman/specgen/markers.hpp                                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — the docblock marker set (design §4; decision marker-registry).
// Markers govern placement, visibility, and extraction rather than
// description content: they never reach the IR, which models wording only.
// This is the single definition of the marker set: the struct and, beside it,
// the registry that spells each marker. The grammar layer parses it
// (docblock.hpp/.cpp, by looking each spelling up here) and the lowering layer
// carries it out of band (lower.hpp) as `lowering::ItemDirectives`, an alias
// for this type rather than a second, hand-copied struct.

#ifndef BEMAN_SPECGEN_MARKERS_HPP
#define BEMAN_SPECGEN_MARKERS_HPP

#include <beman/specgen/foundation/enumeration.hpp>

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace beman::specgen::grammar {

struct Markers {
    // Visibility and synopsis rendering.
    bool                       expos = false;
    std::optional<std::string> expos_name; // \expos(name) override; else derived from the identifier
    bool                       freestanding         = false;
    bool                       freestanding_deleted = false;
    bool                       verbatim_synopsis    = false;

    // Entity-level structure.
    bool                       merge     = false;
    bool                       omit      = false;
    bool                       elsewhere = false; // declaration is supplied by hand elsewhere in the paper
    bool                       describe  = false;
    bool                       also      = false;
    std::optional<std::string> group_id;    // \group <id>, frame-local primary
    std::optional<std::string> also_target; // optional id in \also <id>

    // Itemdecl rendering.
    bool                       constraints_in_decl = false;
    bool                       seebelow            = false;
    std::optional<std::string> seebelow_target; // optional \seebelow argument
    bool                       impdef = false;  // alias RHS only

    // Explicit itemdescr placement for in-class-defined members.
    std::optional<std::string> at_anchor; // \at <anchor>

    // Bodies the front end must extract: \effects-equiv into the placeholder
    // EquivalentTo left on an Effects element, \returns-expr into a Returns
    // element the front end builds from a body that is one `return E;`.
    bool effects_equiv = false;
    bool returns_expr  = false;
};

// The marker registry (design §4; decision marker-registry): one row per marker
// spelling, driving `find_marker` and the marker-vs-marker duplicate checks
// in parse_docblock. Adding a marker means adding a field to `Markers`
// above plus one row here — never a second table or an ad-hoc `if`
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

} // namespace beman::specgen::grammar

template <>
inline constexpr auto beman::specgen::foundation::enumerators<beman::specgen::grammar::MarkerArity> = std::array{
    beman::specgen::grammar::MarkerArity::Flag,
    beman::specgen::grammar::MarkerArity::ParenOptional,
    beman::specgen::grammar::MarkerArity::RestOptional,
    beman::specgen::grammar::MarkerArity::RestRequired,
};

namespace beman::specgen::grammar {

namespace detail {
// Setters for the markers whose value is text, not mere presence. Each
// returns whether it replaced an already-present value: the generic parser
// uses that to raise "duplicate marker" for markers with no dedicated `flag`
// to check instead (arity RestRequired, e.g. \at and \group). Defined in
// markers.cpp; declared here only so `kMarkers` below can take their
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
    std::string_view missing_arg_error     = {}; // used only when arity == RestRequired and the text is empty.
};

// clang-format off
inline constexpr MarkerInfo kMarkers[] = {
    {.spelling = "expos",
     .flag     = &Markers::expos,
     .set_arg  = &detail::set_expos_name,
     .arity    = MarkerArity::ParenOptional},
    {.spelling = "merge",               .flag = &Markers::merge},
    {.spelling = "omit",                .flag = &Markers::omit},
    {.spelling = "elsewhere",           .flag = &Markers::elsewhere},
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
    {.spelling = "returns-expr",        .flag = &Markers::returns_expr},
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

// The registry row for a marker spelling (without its `\`), or nullptr when
// the spelling names no marker.
const MarkerInfo* find_marker(std::string_view name);

} // namespace beman::specgen::grammar

#endif // BEMAN_SPECGEN_MARKERS_HPP
