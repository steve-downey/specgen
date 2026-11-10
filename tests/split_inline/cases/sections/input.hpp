// tests/split_inline/cases/sections/input.hpp                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A header with specgen work already in it (docs/plans/split-inline.md,
// "Where the definition goes"): `\ref` groups in the class, a definition
// region with `\rSec3` sections, and out-of-line siblings on both sides of
// the in-class members. `size()` must land between `front()` and `back()`
// in its section, `clear()` goes after `reset()` at the end of its own,
// `swap()`'s group has no section yet and gets one after the last, and
// `helper()` has no group and goes right after the class.

#ifndef BEMAN_SPECGEN_SPLIT_INLINE_SECTIONS_INPUT_HPP
#define BEMAN_SPECGEN_SPLIT_INLINE_SECTIONS_INPUT_HPP

namespace demo {

class buffer {
  public:
    int helper() const { return count_ * 2; }

    // \ref{buffer.access}, access
    int& front();
    //! \returns The number of elements.
    int  size() const { return count_; }
    int& back();

    // \ref{buffer.mod}, modifiers
    void reset();
    //! \effects Sets the size to zero.
    void clear() { count_ = 0; }

    // \ref{buffer.swap}, swap
    //! \effects Exchanges the sizes.
    void swap(buffer& other) noexcept {
        int t        = count_;
        count_       = other.count_;
        other.count_ = t;
    }

  private:
    int count_ = 0;
};

// \rSec3[buffer.access]{Access}

//! \effects Returns a reference to the first element.
int& buffer::front() { return count_; }

//! \effects Returns a reference to the last element.
int& buffer::back() { return count_; }

// \rSec3[buffer.mod]{Modifiers}

//! \effects Resets the buffer.
void buffer::reset() { count_ = 0; }

} // namespace demo

#endif // BEMAN_SPECGEN_SPLIT_INLINE_SECTIONS_INPUT_HPP
