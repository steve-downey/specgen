// include/beman/specgen/lower.hpp                                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — lowering from the docblock grammar to the semantic IR.
// The grammar layer knows about tags and text; the IR layer knows about the
// wording ontology. This is the seam between them. Reference resolution does
// not happen here: inline code spans carry raw names with empty span tables
// for the front end to resolve against the AST.
// See docs/architecture.md §4 and §7.

#ifndef BEMAN_SPECGEN_LOWER_HPP
#define BEMAN_SPECGEN_LOWER_HPP

#include <beman/specgen/docblock.hpp>
#include <beman/specgen/ir.hpp>

#include <string>
#include <string_view>

namespace beman::specgen::lowering {

// Markers that govern placement, visibility, and extraction rather than
// description content. These never reach the IR — the IR is the wording
// ontology, and none of this is wording. The front end consumes them when it
// attaches markup to declarations.
//
// This is the same set the grammar layer parses (decision marker-registry):
// an alias, not a second hand-copied struct, so the two can never drift.
// See markers.hpp for the field list and per-field documentation.
using ItemDirectives = grammar::Markers;

struct Lowered {
    ir::ItemDescr  descr;
    ItemDirectives directives;
};

// Lower one parsed markup block into canonical-order IR plus its directives.
// An \effects-equiv or \returns-equiv marker produces the corresponding
// element carrying an empty EquivalentTo, marking where extracted code goes.
Lowered lower(const grammar::Docblock&);

// Exposition-only name derivation: strip trailing underscores, then map
// remaining '_' to '-'. `\expos(name)` overrides this; the front end applies
// it to the declaration's identifier, which the grammar layer never sees.
std::string exposid_name(std::string_view identifier);

} // namespace beman::specgen::lowering

#endif // BEMAN_SPECGEN_LOWER_HPP
