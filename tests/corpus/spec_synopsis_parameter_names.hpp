// tests/corpus/spec_synopsis_parameter_names.hpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// `--synopsis-parameter-names drop` (issue #385): the draft's synopses mostly
// declare parameters unnamed, and its item declarations name them. A name the
// declaration uses again, here in a noexcept, stays.
//
// Self-contained (no #includes), so it parses standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_SYNOPSIS_PARAMETER_NAMES_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_SYNOPSIS_PARAMETER_NAMES_HPP

namespace demo {

// \rSec2[demo.syn]{Header `<demo>` synopsis}

//! \returns `n`.
constexpr int identity(int n) { return n; }

/// END [demo.syn]

// \rSec2[demo.box]{Class template `box`}
template <class T>
class box {
  public:
    // \ref{demo.box.cons}, constructors
    constexpr explicit box(const T& value, int count = 1);
    template <class U>
    constexpr box(U&& u) noexcept(noexcept(T(u)));

  private:
    //! \expos
    T val_;
};

// \rSec3[demo.box.cons]{Constructors}

//! \effects Initializes `val` with `value`.
template <class T>
constexpr box<T>::box(const T& value, int) : val_(value) {}

//! \effects Initializes `val` with `u`.
template <class T>
template <class U>
constexpr box<T>::box(U&& u) noexcept(noexcept(T(u))) : val_(u) {}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_SYNOPSIS_PARAMETER_NAMES_HPP
