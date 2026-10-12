// tests/corpus/spec_merge_twin_constraints.hpp                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A `\merge`d defaulted twin and the user-provided member it merges into are
// one specification entity (design §4.3), so the survivor's constraint is
// what the pair shares. The conjunct that only tells the two apart -- the
// survivor's `!X` against the twin's `X` -- is how the implementation picks a
// declaration, not part of the contract, and goes from the synopsis, from an
// itemdecl that keeps the clause, and from the derived Constraints alike.
//
// Each member pins one shape of the cut:
//
//  - the copy constructor keeps the conjunct it shares with its twin, so the
//    synopsis and the derived Constraints both say only that;
//  - the move constructor and the destructor have nothing left once the
//    distinguishing conjunct goes, so the `requires` keyword goes with it and
//    there is no Constraints element at all;
//  - copy assignment leads with the distinguishing conjunct and keeps its
//    clause in the itemdecl (`\constraints-in-decl`), so the cut takes the
//    `&&` to its right, in the synopsis and the itemdecl both;
//  - move assignment nests its distinguishing conjunct inside parentheses
//    with another one, which is not a flat `&&` chain: nothing is cut there,
//    anywhere, rather than the synopsis and the Constraints disagreeing.
//
// Self-contained (no #includes): the traits are stub variable templates, and
// nothing is instantiated, so the bodies need only parse.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_MERGE_TWIN_CONSTRAINTS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_MERGE_TWIN_CONSTRAINTS_HPP

namespace demo {

template <class T>
constexpr bool copyable_v = true;
template <class T>
constexpr bool movable_v = true;
template <class T>
constexpr bool trivial_copy_v = true;
template <class T>
constexpr bool trivial_move_v = true;
template <class T>
constexpr bool trivial_dtor_v = true;
template <class T>
constexpr bool trivial_assign_v = true;

template <class T>
class holder {
  public:
    // \ref{demo.holder.cons}, construct/copy/destroy
    constexpr holder(const holder& other)
        requires copyable_v<T> && (!trivial_copy_v<T>);
    //! \merge
    constexpr holder(const holder&)
        requires copyable_v<T> && trivial_copy_v<T>
    = default;

    constexpr holder(holder&& other)
        requires(!trivial_move_v<T>);
    //! \merge
    constexpr holder(holder&&)
        requires trivial_move_v<T>
    = default;

    constexpr ~holder()
        requires(!trivial_dtor_v<T>);
    //! \merge
    constexpr ~holder()
        requires trivial_dtor_v<T>
    = default;

    constexpr holder& operator=(const holder& other)
        requires(!trivial_assign_v<T>) && copyable_v<T>;
    //! \merge
    constexpr holder& operator=(const holder&)
        requires trivial_assign_v<T> && copyable_v<T>
    = default;

    constexpr holder& operator=(holder&& other)
        requires movable_v<T> && (copyable_v<T> && !trivial_assign_v<T>);
    //! \merge
    constexpr holder& operator=(holder&&)
        requires movable_v<T> && trivial_assign_v<T>
    = default;

  private:
    //! \expos
    T value_;
};

// \rSec2[demo.holder.cons]{Construct/copy/destroy}

//! \effects Copies the held value.
template <class T>
constexpr holder<T>::holder(const holder& other)
    requires copyable_v<T> && (!trivial_copy_v<T>)
    : value_(other.value_) {}

//! \effects Moves the held value.
template <class T>
constexpr holder<T>::holder(holder&& other)
    requires(!trivial_move_v<T>)
    : value_(other.value_) {}

//! \effects Destroys the held value.
template <class T>
constexpr holder<T>::~holder()
    requires(!trivial_dtor_v<T>)
{}

//! \constraints-in-decl
//! \effects Copy-assigns the held value.
//! \returns `*this`.
template <class T>
constexpr holder<T>& holder<T>::operator=(const holder& other)
    requires(!trivial_assign_v<T>) && copyable_v<T>
{
    value_ = other.value_;
    return *this;
}

//! \effects Move-assigns the held value.
//! \returns `*this`.
template <class T>
constexpr holder<T>& holder<T>::operator=(holder&& other)
    requires movable_v<T> && (copyable_v<T> && !trivial_assign_v<T>)
{
    value_ = other.value_;
    return *this;
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_MERGE_TWIN_CONSTRAINTS_HPP
