// include/beman/specgen/synopsis_edits.hpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — the edit algebra behind subtractive synopsis extraction (design
// §3.4): text edits at absolute offsets, ordered for a back-to-front scatter
// under an overlap watermark, and the rule that an enclosing mask wins over
// the edits nested inside it. Clang-free: the front end finds the ranges.

#ifndef BEMAN_SPECGEN_SYNOPSIS_EDITS_HPP
#define BEMAN_SPECGEN_SYNOPSIS_EDITS_HPP

#include <string>
#include <vector>

namespace beman::specgen::synopsis_edits {

// One text edit against the extracted synopsis substring, in absolute
// main-file byte offsets (translated to offsets relative to the class span
// just before being applied — see extract_synopsis).
struct SynopsisEdit {
    unsigned    begin = 0;
    unsigned    end   = 0;
    std::string replacement;
};

// Order edits for a back-to-front scatter under an overlap watermark:
// descending begin and, among edits that share a begin, the longer first, so
// an enclosing removal is applied and the watermark skips the edits it
// subsumes. Begin alone leaves equal-begin edits in std::sort's unspecified
// tie order, and an anonymous struct's whole-line removal applied before its
// declarator's longer one left the field name behind (issue #147). Stable, so
// edits equal in both offsets -- two insertions at one point -- keep push order.
void sort_back_to_front(std::vector<SynopsisEdit>& edits);

bool edits_overlap(const SynopsisEdit& a, const SynopsisEdit& b);

// A see-below replacement masks the expression's implementation spelling as
// a whole. Namespace/exposition rewrites nested inside it must therefore lose
// to the enclosing edit rather than causing the descending-offset watermark
// to skip the see-below edit after applying an inner one.
void add_dominant_edit(std::vector<SynopsisEdit>& edits, SynopsisEdit dominant);

} // namespace beman::specgen::synopsis_edits

#endif // BEMAN_SPECGEN_SYNOPSIS_EDITS_HPP
