// tests/corpus/spec_class_deduction_guide.hpp                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A class template's deduction guides, declared after the class in the same
// section, are written after the class in its synopsis, inside the same
// namespace, as the draft writes [expected.un.general] (issue #384). `\omit`
// leaves one out, as it does in a gathered header synopsis.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_CLASS_DEDUCTION_GUIDE_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_CLASS_DEDUCTION_GUIDE_HPP

namespace demo {
// \rSec2[demo.holder]{Class template `holder`}
template <class I>
class holder {
  public:
    // \ref{demo.holder.cons}, constructors
    constexpr explicit holder(I ptr);
    constexpr holder(I ptr, int count);

  private:
    //! \expos
    I ptr_;
};

template <class I>
holder(I) -> holder<I>;

//! \omit
template <class I>
holder(I, int) -> holder<I>;

// \rSec3[demo.holder.cons]{Constructors}

//! \effects Initializes `ptr` with `ptr`.
template <class I>
constexpr holder<I>::holder(I ptr) : ptr_(ptr) {}

//! \effects Initializes `ptr` with `ptr`.
template <class I>
constexpr holder<I>::holder(I ptr, int) : ptr_(ptr) {}
} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_CLASS_DEDUCTION_GUIDE_HPP
