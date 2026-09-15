// tests/corpus/spec_paper_sibling.hpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (issues #109 and #113): one header of a
// two-header paper, the one that *documents* `blend_policy`. On its own it is an
// ordinary, fully §9-clean document; its role in the corpus is to be the
// sibling whose documented names spec_paper_main.hpp is entitled to use. It
// deliberately supplies both a synopsis-rostered class and a normative
// variable-template ItemDecl: issue #109's first fix admitted the former but
// accidentally omitted the latter. The pair models a real paper generated
// one run per header: each header is its own specgen document, and the paper
// is the unit that has to be internally consistent. It also supplies two
// declarations hand-authored elsewhere in the paper and an exposition-only
// namespace using-declaration, the two non-rendered shapes from issue #113.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_PAPER_SIBLING_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_PAPER_SIBLING_HPP

#include "support/spec_paper_traits.hpp"

namespace demo {

//! \elsewhere
struct tag_t {
    explicit tag_t() = default;
};

//! \elsewhere
inline constexpr tag_t tag{};

namespace detail {
//! \expos
using support::is_void_v;
} // namespace detail

//! \remarks This variable template is the lookup point for the alpha policy
//! of a type. A program may specialize it.
template <class T>
inline constexpr int alpha_policy = 0;

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
