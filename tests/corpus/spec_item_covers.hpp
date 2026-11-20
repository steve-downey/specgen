// tests/corpus/spec_item_covers.hpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// `\covers constraints P` on an item, without `\asdraft` (issue #386). The
// draft states a copy constructor's condition in *Remarks*, "defined as
// deleted unless ...", and gives it no *Constraints*. A conjunct the item's
// requires-clause derives and a claim names leaves the derived element; the
// element is gone once every conjunct is claimed, and renders the rest when
// some are not. An empty `\constraints` removes the derived element whole.
//
// Self-contained (no #includes), so it parses standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_ITEM_COVERS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_ITEM_COVERS_HPP

namespace demo {

template <class T>
inline constexpr bool is_copyable_v = true;
template <class T>
inline constexpr bool is_trivially_copyable_v = false;

// \rSec2[demo.box]{Class template `box`}
template <class T>
class box {
  public:
    // \ref{demo.box.cons}, constructors
    constexpr box(const box& rhs)
        requires(is_copyable_v<T> && !(is_trivially_copyable_v<T>));
    constexpr box(box&& rhs)
        requires(is_copyable_v<T> && !(is_trivially_copyable_v<T>));
    constexpr box(int n)
        requires(is_copyable_v<T>);

  private:
    //! \expos
    T val_;
};

// \rSec3[demo.box.cons]{Constructors}

//! \covers constraints is_copyable_v<T>
//! \covers constraints !(is_trivially_copyable_v<T>)
//! \effects Initializes `val` with `rhs.val`.
//! \remarks This constructor is defined as deleted unless `is_copyable_v<T>`
//! is `true`. This constructor is trivial if `is_trivially_copyable_v<T>` is
//! `true`.
template <class T>
constexpr box<T>::box(const box& rhs)
    requires(is_copyable_v<T> && !(is_trivially_copyable_v<T>))
    : val_(rhs.val_) {}

//! \covers constraints is_copyable_v<T>
//! \effects Initializes `val` with `rhs.val`.
//! \remarks This constructor is defined as deleted unless `is_copyable_v<T>`
//! is `true`.
template <class T>
constexpr box<T>::box(box&& rhs)
    requires(is_copyable_v<T> && !(is_trivially_copyable_v<T>))
    : val_(rhs.val_) {}

//! \constraints
//! \effects Initializes `val` with `n`.
template <class T>
constexpr box<T>::box(int n)
    requires(is_copyable_v<T>)
    : val_(n) {}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_ITEM_COVERS_HPP
