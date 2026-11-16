// tests/corpus/spec_expos_code_spans.hpp                           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// An exposition-only member named inside an authored code span renders under
// its exposition name wherever in the span it appears (issue #286). Only a
// span that was *exactly* the member's C++ name used to be renamed, so
// `` `value_ == 0` `` put the implementation's spelling, trailing underscore
// and all, into the wording, and validation said nothing.
//
// Either spelling names the member: the header's (`value_`, `p_`) or the
// paper's (`value`, `ptr`, `has-next`), which is how wording transcribed from
// a paper is written. `get()` uses the first, `twice()` the second, and they
// render alike. `size()` is extracted code, the form both should take.
//
// The exposition name is text, with nothing behind it to resolve, so a name
// the wording already uses for something else is not read as one:
// `rebind(int value)` has a parameter called `value`, and in its description
// `value` is the parameter while `value_` is still the member. `count` is an
// exposition-only member whose exposition name is also a public member
// function's, so `count()` in prose stays the function.
//
// A string literal's contents are text, not names.
//
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_CODE_SPANS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_CODE_SPANS_HPP

namespace demo {

class cursor {
  public:
    // \ref{cursor.obs}, observers
    bool empty() const;
    int  get() const;
    int  twice() const;
    int  size() const;
    int  count() const;
    bool more() const;

    // \ref{cursor.mod}, modifiers
    void rebind(int value);

  private:
    //! \expos
    int value_ = 0;
    //! \expos(ptr)
    int* p_ = nullptr;
    //! \expos
    bool has_next_ = false;
    //! \expos(count)
    int count_ = 0;
};

// \rSec3[cursor.obs]{Observers}

//! \returns `value_`.
inline bool cursor::empty() const { return value_ == 0; }

//! \returns `true` if `value_ == 0`, otherwise `*p_ == value_`.
inline int cursor::get() const { return value_; }

//! \returns `value` if `value == 0`, otherwise `*ptr`.
inline int cursor::twice() const { return value_; }

//! \returns-expr
inline int cursor::size() const { return value_ + *p_; }

//! \returns The number of calls, which `count()` also reports through `count_`.
inline int cursor::count() const { return count_; }

//! \returns `has-next && rhs.has_next_`, where `"has_next_"` is not a name.
inline bool cursor::more() const { return has_next_; }

// \rSec3[cursor.mod]{Modifiers}

//! \effects Assigns `value` to `value_`.
//! \ensures `value_ == value` is `true`.
inline void cursor::rebind(int value) { value_ = value; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_CODE_SPANS_HPP
