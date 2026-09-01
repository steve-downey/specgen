// include/beman/specgen/markers.hpp                                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — the docblock marker set (design §4; decision marker-registry).
// Markers govern placement, visibility, and extraction rather than
// description content: they never reach the IR, which models wording only.
// This is the single definition of the marker set. The grammar layer parses
// it (docblock.hpp/.cpp, via the marker registry in docblock.hpp) and the
// lowering layer carries it out of band (lower.hpp) as
// `lowering::ItemDirectives`, an alias for this type rather than a second,
// hand-copied struct.

#ifndef BEMAN_SPECGEN_MARKERS_HPP
#define BEMAN_SPECGEN_MARKERS_HPP

#include <optional>
#include <string>

namespace beman::specgen::grammar {

struct Markers {
    // Visibility and synopsis rendering.
    bool                       expos = false;
    std::optional<std::string> expos_name; // \expos(name) override; else derived from the identifier
    bool                       freestanding         = false;
    bool                       freestanding_deleted = false;
    bool                       verbatim_synopsis    = false;

    // Entity-level structure.
    bool                       merge    = false;
    bool                       omit     = false;
    bool                       describe = false;
    bool                       also     = false;
    std::optional<std::string> group_id;    // \group <id>, frame-local primary
    std::optional<std::string> also_target; // optional id in \also <id>

    // Itemdecl rendering.
    bool                       constraints_in_decl = false;
    bool                       seebelow            = false;
    std::optional<std::string> seebelow_target; // optional \seebelow argument
    bool                       impdef = false;  // alias RHS only

    // Explicit itemdescr placement for in-class-defined members.
    std::optional<std::string> at_anchor; // \at <anchor>

    // Bodies the front end must extract and substitute into the placeholder
    // EquivalentTo left on the corresponding element.
    bool effects_equiv = false;
    bool returns_equiv = false;
};

} // namespace beman::specgen::grammar

#endif // BEMAN_SPECGEN_MARKERS_HPP
