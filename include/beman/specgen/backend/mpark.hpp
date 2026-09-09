// include/beman/specgen/backend/mpark.hpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — mpark/wg21 serializer.
// Emits pandoc markdown for the mpark/wg21 paper framework: `::: wording`
// divs with `[#]{.pnum}` auto-numbering, `[stable.name]{- .sref}` headings,
// ```cpp fences whose spans use the framework's embedded-Markdown `$…$`
// convention. Like the LaTeX backend these are standalone fragments for
// transclusion; the including paper owns its front matter and framing.
// See docs/architecture.md §8.

#ifndef BEMAN_SPECGEN_BACKEND_MPARK_HPP
#define BEMAN_SPECGEN_BACKEND_MPARK_HPP

#include <beman/specgen/ir.hpp>

#include <string>

namespace beman::specgen::backend::mpark {

struct Options {
    // Markdown heading level of a top-level Section; nested sections descend
    // from here. Two by default, matching the framework's own wording
    // examples (`## [intro.compliance.general]{.sref} {-}`), which leaves `#`
    // to the including paper's own top-level headings.
    //
    // This is the counterpart of latex::Options::base_section_depth and not
    // the same number: `\rSec3` is the draft's default library split
    // granularity, while a paper's wording sections start one or two levels
    // down from its title.
    int base_heading_level = 2;

    // Wrap the fragment in an `::: add` editing-instruction div and
    // number its paragraphs with mpark's *placeholder* form (`x`, `x+1`, ...)
    // instead of the auto-numbering `[#]{.pnum}`, so inserting the wording
    // does not renumber the wording it is inserted into.
    //
    // Whole-fragment, not per-entity: which parts of a fragment are new is a
    // property of the edit being proposed and the IR records nothing about it
    // (design §7 puts add/rm markup behind a diff of two header revisions,
    // "out of scope now, by hand at first"). The paper still authors the
    // instruction that says where the wording goes; this supplies the div and
    // the numbering, which are the mechanical halves.
    bool paper_mode = false;

    // A stable name under this dotted prefix (equal to it, or one segment
    // deeper) is the paper's own proposed clause, not yet in the srefs
    // database `.sref` looks up -- so its class is dropped everywhere a
    // stable name is rendered (a `\ref` in a synopsis comment, an `\iref`
    // cross-reference, a Section heading), leaving the bare `[name]` the
    // draft itself prints for a clause with no number yet (issue #89).
    // Empty (the default) changes nothing: every stable name keeps `.sref`,
    // exactly as before this option existed. A name outside the prefix --
    // a citation of a clause the draft already has -- is unaffected either
    // way, which is the point: this says which names are new, not which
    // backend feature to turn off.
    std::string new_root;
};

// Rendering returns the fragment; a caller that has a sink writes it once
// (decision format-print-output — the LaTeX backend's boundary rule).
std::string render_to_string(const ir::Document&, const Options& = {});
std::string render_to_string(const ir::SpecItem&, const Options& = {});

} // namespace beman::specgen::backend::mpark

#endif // BEMAN_SPECGEN_BACKEND_MPARK_HPP
