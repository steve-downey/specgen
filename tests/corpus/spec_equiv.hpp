// tests/corpus/spec_equiv.hpp                                      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (design §4.2 / §5.2: `\effects-equiv` and
// `\returns-equiv` body extraction). Two out-of-line member definitions under
// one `\rSec3` section:
//
//   - `get()` carries `\returns-equiv`: its single-return body extracts into a
//     Returns element's "Equivalent to:" block (`return value_;`).
//   - `reset()` carries `\effects-equiv`: a retained local alias precedes the
//     maximal derivable static_assert run. Per design §5.2, that assertion is
//     dropped from the extracted Effects body, while the alias and an assertion
//     after executable code remain. Doxygen and specgen comments are stripped
//     from the body while draft-form comments and comment spellings inside
//     string literals remain. This pins the alias prologue and the classifier.
//
// Both data members carry `\expos` because both are *named by those extracted
// bodies*, and design §9's leakage checker makes that an error for an
// unmarked private member: an "Equivalent to:" block naming something the
// reader cannot see is not wording. Marking them exposition-only is the first
// of §9's three fixits and the one the draft uses — so the bodies here render
// as `\exposid{value}` and `\exposid{engaged}` (expos-use rewriting),
// which is what the draft would print.
//
// Self-contained (no #includes): `is_trivial_v` is a stub variable template,
// not the real <type_traits>, so the assert resolves standalone under
// -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_EQUIV_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_EQUIV_HPP

namespace demo {

template <class T>
constexpr bool is_trivial_v = true;

template <class T>
class box {
  public:
    // \ref{box.observe}, observers
    constexpr const T& get() const;
    constexpr void     reset();

  private:
    //! \expos
    T value_;
    //! \expos
    bool engaged_;
};

// \rSec3[box.observe]{Observers}

//! \returns-equiv
template <class T>
constexpr const T& box<T>::get() const {
    return value_;
}

//! \effects-equiv
template <class T>
constexpr void box<T>::reset() {
    using value_type = T;
    static_assert(is_trivial_v<T>);
    /// Whole-line Doxygen is not draft wording.
    /** Whole-line Doxygen block is not draft wording. */
    //! Whole-line specgen markup is not body wording.
    /*! Whole-line specgen block is not body wording. */
    value_ = T();                          /// Trailing Doxygen is removed with no blank line.
    static_assert(sizeof(value_type) > 0); // after real code: retained
    // A draft-form body comment survives.
    /* A draft-form block comment survives. */
    [[maybe_unused]] const char* line_spelling  = "/// string content survives";
    [[maybe_unused]] const char* block_spelling = "/** string content survives */";
    [[maybe_unused]] const char* raw_spelling   = R"body(//! raw string content survives
/** raw string content survives */)body";
    engaged_                                    = false; //! Trailing specgen markup is removed before the newline.
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_EQUIV_HPP
