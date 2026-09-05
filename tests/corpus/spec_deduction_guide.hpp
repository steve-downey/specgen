// tests/corpus/spec_deduction_guide.hpp                            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A deduction guide inside a gathered header synopsis (issue #22). Clang
// synthesizes *implicit* guides whose locations point at the constructor's
// and the class's own tokens; collected as top-level decls, they replayed the
// class body into the gathered synopsis as raw source — markup comments and
// all — with `<deduction guide for holder>` spliced over the constructor's
// name and the code block left unterminated. Implicit decls are compiler
// synthesis, never document-tree items. The *authored* guide renders
// verbatim and unindexed (its AST name is not its source spelling), and
// `\omit` removes one exactly as it does any other declaration.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_DEDUCTION_GUIDE_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_DEDUCTION_GUIDE_HPP

// \rSec2[demo.syn]{Header synopsis}

namespace demo {

template <typename I>
class holder {
    //! \expos
    I ptr_;

  public:
    //! \effects Initializes the holder with `ptr`.
    constexpr explicit holder(I ptr);

    //! \returns `ptr_`.
    constexpr I get() const;
};

template <typename I>
holder(I) -> holder<I>;

//! \omit
template <typename I>
holder(I*) -> holder<I*>;

} // namespace demo

/// END [demo.syn]

// \rSec3[demo.holder]{Class template `holder`}

namespace demo {

//! \effects Initializes the holder with `ptr`.
template <typename I>
constexpr holder<I>::holder(I ptr) : ptr_(ptr) {}

//! \returns `ptr_`.
template <typename I>
constexpr I holder<I>::get() const {
    return ptr_;
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_DEDUCTION_GUIDE_HPP
