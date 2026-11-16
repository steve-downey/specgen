// tests/corpus/diff/qualifier.after.hpp                           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (decision hermetic-corpus): the *current* half of
// the `decls` context's fixture pair; see qualifier.before.hpp for what
// changed between the two. Self-contained (no #includes) and built from
// bool/int only, so it parses standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_DIFF_QUALIFIER_AFTER_HPP
#define BEMAN_SPECGEN_CORPUS_DIFF_QUALIFIER_AFTER_HPP

namespace demo {

class span_like {
  public:
    // \ref{span.like.obs}, observers
    constexpr int  size() const;
    constexpr bool empty() const;
    constexpr int  front() const;

  private:
    int size_ = 0;
};

// \rSec3[span.like.obs]{Observers}

//! \returns The number of elements.
//! \complexity Constant.
constexpr int span_like::size() const { return size_; }

//! \returns `size() == 0`.
constexpr bool span_like::empty() const { return size_ == 0; }

//! \expects the span is not empty.
//! \returns The first element.
constexpr int span_like::front() const { return size_; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_DIFF_QUALIFIER_AFTER_HPP
