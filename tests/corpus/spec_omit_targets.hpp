// tests/corpus/spec_omit_targets.hpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A targeted `\omit` (issue #385): what an implementation declares beyond the
// standard, left out of the wording. `\omit noexcept` drops a strengthened
// exception specification, which [res.on.exception.handling] lets an
// implementation add, from the synopsis and the item declaration alike, and
// `\omit default` drops `= default` from a special member the standard only
// declares. The declaration itself stays, as it does not for a bare `\omit`.
// In a gathered header synopsis the same holds for a free function.
//
// Self-contained (no #includes), so it parses standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_OMIT_TARGETS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_OMIT_TARGETS_HPP

namespace demo {

// \rSec2[demo.syn]{Header `<demo>` synopsis}

//! \omit noexcept
//! \returns `n`.
constexpr int identity(int n) noexcept { return n; }

/// END [demo.syn]

// \rSec2[demo.box]{Class template `box`}
template <class T>
class box {
  public:
    // \ref{demo.box.cons}, constructors
    constexpr box() noexcept(noexcept(T()));
    //! \omit default
    constexpr box(const box&) noexcept = default;
    constexpr explicit box(int n) noexcept;

  private:
    //! \expos
    T val_;
};

// \rSec3[demo.box.cons]{Constructors}

//! \omit noexcept
//! \effects Value-initializes `val`.
template <class T>
constexpr box<T>::box() noexcept(noexcept(T())) : val_() {}

//! \omit noexcept
//! \effects Initializes `val` with `n`.
template <class T>
constexpr box<T>::box(int n) noexcept : val_(n) {}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_OMIT_TARGETS_HPP
