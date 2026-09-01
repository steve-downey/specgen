// include/beman/specgen/conjuncts.hpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — rendering a conjunction as wording.
// Constraints and Mandates are both conjunctions read back out of code: a
// requires-clause and a static_assert prefix respectively. How the conjuncts
// are laid out is a presentation decision shared by both, so it lives here
// rather than in either derivation.
// See docs/architecture.md §5.3.

#ifndef BEMAN_SPECGEN_CONJUNCTS_HPP
#define BEMAN_SPECGEN_CONJUNCTS_HPP

#include <beman/specgen/ir.hpp>

#include <cstddef>
#include <vector>

namespace beman::specgen::conjuncts {

struct Options {
    // At most this many conjuncts join into a single sentence; beyond it, the
    // conjunction becomes a bulleted list.
    std::size_t sentence_threshold = 3;
};

// Join conjuncts into one sentence: "A", "A and B", "A, B, and C".
// A terminating period is added; conjuncts themselves carry no punctuation.
ir::Paragraph join_sentence(const std::vector<ir::Paragraph>&);

// Bulleted form: every item ends in a comma except the last, which ends in a
// period, following the draft's own enumerated-condition style.
ir::Itemize as_itemize(const std::vector<ir::Paragraph>&);

// Place a conjunction into an element, choosing sentence or list form by the
// threshold. Existing prose in the element is left alone; the conjunction is
// appended after it.
void render_into(const std::vector<ir::Paragraph>&, ir::DescriptionElement&, const Options& = {});

} // namespace beman::specgen::conjuncts

#endif // BEMAN_SPECGEN_CONJUNCTS_HPP
