// include/beman/specgen/backend/latex.hpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — draft LaTeX serializer.
// Emits standalone fragments for transclusion: the including document owns
// framing, page structure, and numbering. Output uses the working draft's own
// macros (\itemdecl, \itemdescr, \pnum, \tcode, \codeblock, the description
// macros), so a fragment drops into the draft sources unchanged.
// See docs/architecture.md §8.

#ifndef BEMAN_SPECGEN_BACKEND_LATEX_HPP
#define BEMAN_SPECGEN_BACKEND_LATEX_HPP

#include <beman/specgen/ir.hpp>

#include <string>

namespace beman::specgen::backend::latex {

struct Options {
    // Depth of a top-level Section; nested sections descend from here.
    // The draft splits library wording at \rSec3 by default.
    int base_section_depth = 3;
};

// Rendering returns the fragment; a caller that has a sink writes it once.
// There are deliberately no `render(..., std::ostream&)` overloads (decision
// format-print-output): the only thing one could do — insert a string this
// backend's algebra has *already* computed — belongs to the caller's sink.
std::string render_to_string(const ir::Document&, const Options& = {});
std::string render_to_string(const ir::SpecItem&, const Options& = {});

} // namespace beman::specgen::backend::latex

#endif // BEMAN_SPECGEN_BACKEND_LATEX_HPP
