// tests/corpus/diff/qualifier.before.hpp                           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (decision hermetic-corpus): the *baseline* half
// of the `decls` context's fixture pair (docs/plans/wording-diff.md). Its
// twin, qualifier.after.hpp, is this header one revision later, in the shape
// of a paper that adds a qualifier across an interface: `size()` and `empty()`
// gain `constexpr` and nothing else, so their descriptions are unchanged;
// `front()` gains `constexpr` and has its *Preconditions* reworded, so its
// description changed too. Self-contained (no #includes) and built from
// bool/int only, so it parses standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_DIFF_QUALIFIER_BEFORE_HPP
#define BEMAN_SPECGEN_CORPUS_DIFF_QUALIFIER_BEFORE_HPP

namespace demo {

class span_like {
  public:
    // \ref{span.like.obs}, observers
    int  size() const;
    bool empty() const;
    int  front() const;

  private:
    int size_ = 0;
};

// \rSec3[span.like.obs]{Observers}

//! \returns The number of elements.
//! \complexity Constant.
int span_like::size() const { return size_; }

//! \returns `size() == 0`.
bool span_like::empty() const { return size_ == 0; }

//! \expects `empty()` is `false`.
//! \returns The first element.
int span_like::front() const { return size_; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_DIFF_QUALIFIER_BEFORE_HPP
