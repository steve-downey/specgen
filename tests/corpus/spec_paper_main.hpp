// tests/corpus/spec_paper_main.hpp                                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (issue #109): the other header of
// spec_paper_sibling.hpp's two-header paper. Its wording names
// `blend_policy`, which the sibling declares *and documents* — so one run
// over this header alone reports the class and the normative variable
// template `alpha_policy` as foreign (the standing finding
// `paper_main_validate` pins), while a run given both documents' IR validates
// clean (`paper_union` pins that): both names are specified by the paper, just
// across a header boundary. This is the case where none of the solo
// diagnostic's local fixits applies — the names are not exposition-only, one
// run renders one header by design, and wording may name the operation it is
// defined in terms of.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_PAPER_MAIN_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_PAPER_MAIN_HPP

#include "spec_paper_sibling.hpp"

namespace demo {

template <class T>
class mixer {
  public:
    // \ref{paper.mix}, mixing
    int mix(const T& part) const
        requires(alpha_policy<T> == 0) && requires(const blend_policy& policy) { policy.strength(); };
};

// \rSec3[paper.mix]{Mixing}

//! \returns The blended value.
template <class T>
int mixer<T>::mix(const T& part) const
    requires(alpha_policy<T> == 0) && requires(const blend_policy& policy) { policy.strength(); }
{
    return 0;
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_PAPER_MAIN_HPP
