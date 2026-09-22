// tests/corpus/spec_constraints.hpp                                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (design §5.1: Constraints derivation).
// `box`'s converting constructor carries a trailing requires-clause with four
// top-level `&&` conjuncts exercising every phrasing case: a concept-id
// (`copyable<U>`), a plain trait (`is_constructible_v<T, const U&>`), a
// parenthesized negation (`(!is_same_v<T, U>)`), and another plain trait
// (`is_convertible_v<U, T>`). Four conjuncts is past conjuncts::Options's
// sentence_threshold (3), so the derived Constraints element renders as an
// itemize. Self-contained (no #includes): the traits/concept below are stub
// variable templates and a concept, not the real <type_traits>/<concepts>,
// so the requires-clause resolves standalone under -std=c++2c.
//
// The requires-clause must be removed from the itemdecl (design §5.1
// "Default: requires-clause is removed from the itemdecl") — extract_itemdecl
// truncates at the in-class decl's own trailing requires-clause.
//
// `put_head`/`put_trailing` write the *same* two conjuncts in the two
// equivalent clause positions, so the goldens state the invariant directly:
// which position the author picked decides nothing about the wording (issue
// #119). Two conjuncts is under the sentence_threshold, so these render as a
// joined sentence rather than the itemize above. `put_in_decl` pins that
// `\constraints-in-decl` keeps a head clause in the itemdecl and suppresses
// the derivation, the same as it does for a trailing one.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_CONSTRAINTS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_CONSTRAINTS_HPP

namespace demo {

template <class T, class... Args>
constexpr bool is_constructible_v = true;
template <class A, class B>
constexpr bool is_same_v = false;
template <class A, class B>
constexpr bool is_convertible_v = true;

template <class T>
concept copyable = is_constructible_v<T, const T&>;

namespace detail {
//! \expos
template <class T>
constexpr bool is_allowed = true;
//! \expos
template <class T, class U>
concept is_compatible = detail::is_allowed<T> && demo::is_convertible_v<U, T>;
} // namespace detail

template <class T>
class box {
  public:
    // \ref{box.cons}, constructors
    template <class U>
    box(const box<U>& other)
        requires copyable<U> && is_constructible_v<T, const U&> && (!is_same_v<T, U>) && is_convertible_v<U, T>;

    template <class U>
    void assign(U&& value)
        requires detail::is_compatible<T, U> && detail::is_allowed<U>;

    template <class U>
        requires is_constructible_v<T, U> && (!is_same_v<U, int>)
    void put_head(U&& value);

    template <class U>
    void put_trailing(U&& value)
        requires is_constructible_v<T, U> && (!is_same_v<U, int>);

    template <class U>
        requires is_convertible_v<U, T>
    void put_in_decl(U&& value);

  private:
    T value_;
};

// \rSec3[box.cons]{Constructors}

//! \effects Initializes the box from `other`.
template <class T>
template <class U>
box<T>::box(const box<U>& other)
    requires copyable<U> && is_constructible_v<T, const U&> && (!is_same_v<T, U>) && is_convertible_v<U, T>
{
    value_ = T();
}

//! \constraints `U` is not `void`.
//! \effects Assigns `value` to the box.
template <class T>
template <class U>
void box<T>::assign(U&& value)
    requires detail::is_compatible<T, U> && detail::is_allowed<U>
{
    value_ = T();
}

//! \effects Stores `value`.
template <class T>
template <class U>
    requires is_constructible_v<T, U> && (!is_same_v<U, int>)
void box<T>::put_head(U&& value) {
    value_ = T();
}

//! \effects Stores `value`.
template <class T>
template <class U>
void box<T>::put_trailing(U&& value)
    requires is_constructible_v<T, U> && (!is_same_v<U, int>)
{
    value_ = T();
}

//! \constraints-in-decl
//! \effects Stores `value`.
template <class T>
template <class U>
    requires is_convertible_v<U, T>
void box<T>::put_in_decl(U&& value) {
    value_ = T();
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_CONSTRAINTS_HPP
