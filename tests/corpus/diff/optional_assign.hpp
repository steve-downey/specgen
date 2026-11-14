// tests/corpus/diff/optional_assign.hpp                            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (decision hermetic-corpus): one subclause of
// `optional` -- [optional.assign], reduced to the assignment from `nullopt_t`
// -- for the wording diff against the *working draft*, `--baseline-draft`
// (docs/plans/draft-import.md). The baseline is
// tests/corpus/standard-slices/optional.assign.slice, so what the diff marks
// is the difference between this header's wording and the draft's: the
// description of the first item is reworded, and the draft's other items,
// which this header does not declare, come out removed.
//
// Self-contained and parsed standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_DIFF_OPTIONAL_ASSIGN_HPP
#define BEMAN_SPECGEN_CORPUS_DIFF_OPTIONAL_ASSIGN_HPP

namespace demo {

struct nullopt_t {
    //! \omit
    explicit constexpr nullopt_t(int) {}
};

template <class T>
class optional {
  public:
    // \ref{optional.assign}, assignment
    constexpr optional& operator=(nullopt_t) noexcept;

  private:
    bool engaged_ = false;
};

// \rSec3[optional.assign]{Assignment}

//! \effects Destroys the contained value, if there is one.
//! \ensures `*this` does not contain a value.
//! \returns `*this`.
template <class T>
constexpr optional<T>& optional<T>::operator=(nullopt_t) noexcept {
    engaged_ = false;
    return *this;
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_DIFF_OPTIONAL_ASSIGN_HPP
