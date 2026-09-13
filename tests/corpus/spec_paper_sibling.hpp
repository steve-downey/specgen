// tests/corpus/spec_paper_sibling.hpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (issue #109): one header of a two-header
// paper, the one that *documents* `blend_policy`. On its own it is an
// ordinary, fully §9-clean document; its role in the corpus is to be the
// sibling whose documented names spec_paper_main.hpp is entitled to use.
// The pair models a real paper generated one run per header: each header is
// its own specgen document, and the paper is the unit that has to be
// internally consistent.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_PAPER_SIBLING_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_PAPER_SIBLING_HPP

namespace demo {

class blend_policy {
  public:
    // \ref{paper.blend}, observers
    int strength() const;
};

// \rSec3[paper.blend]{Blend policy}

//! \returns The configured strength.
inline int blend_policy::strength() const { return 1; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_PAPER_SIBLING_HPP
