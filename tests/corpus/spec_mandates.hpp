// tests/corpus/spec_mandates.hpp                                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (design §5.2: Mandates derivation).
// `widget<T>::emplace`'s out-of-line body opens with a *static_assert prefix*
// that derives into a Mandates element:
//   - `static_assert(is_constructible_v<T, Args...>);`     — one plain trait
//   - `static_assert(is_move_constructible_v<T> && !is_const_v<T>);`
//                                                          — a top-level `&&`
//     split into two conjuncts, the second a `!`-negation ("is `false`")
// Three conjuncts total is within conjuncts::Options's sentence_threshold (3),
// so the derived Mandates renders as one "and"-joined sentence rather than an
// itemize (the Constraints golden already covers the itemize path).
//
// The prefix is *maximal*: the trailing `static_assert(sizeof(T) > 0);` sits
// after a real statement (`value_ = T();`), so it is not part of the prefix and
// must be excluded from the derivation.
//
// Also the class-scope half of design §5.2: `widget<T>` carries two
// direct static_asserts, separated by ordinary member declarations to prove
// this is a class-scope collection rather than a leading-prefix rule. Their
// three conjuncts become one adjacent general-subclause paragraph and both
// assertions are removed from the synopsis; the diagnostic message is not
// specification wording and is dropped.
//
// `widget<T>::shrink` authors a `\mandates` that replaces the body's derived
// wording. The derived conjunct remains on that authored element as
// validator-only drift evidence. Because the two conditions are unrelated,
// the drift check stays silent, keeping `golden.mandates.validate`
// clean per corpus convention; the drift-positive cases (an authored
// twin that duplicates or contradicts its derived one) are
// `tests/golden/validate_mandates_drift`'s hand-written IR instead, on
// purpose -- a corpus header is expected to validate clean.
//
// Self-contained (no #includes): the traits below are stub variable templates,
// not the real <type_traits>, so the asserts resolve standalone under
// -std=c++2c. The authored `\effects` element exercises canonical ordering —
// Mandates sorts ahead of Effects.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_MANDATES_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_MANDATES_HPP

namespace demo {

template <class T, class... Args>
constexpr bool is_constructible_v = true;
template <class T>
constexpr bool is_move_constructible_v = true;
template <class T>
constexpr bool is_const_v = false;

template <class T>
class widget {
    static_assert(is_move_constructible_v<T> && !is_const_v<T>, "T must be movable");

  public:
    // \ref{widget.mod}, modifiers
    template <class... Args>
    T&   emplace(Args&&... args);
    void shrink();

    static_assert(is_constructible_v<T, T>);

  private:
    T value_;
};

// \rSec3[widget.mod]{Modifiers}

//! \effects Constructs the contained value in place from `args`.
template <class T>
template <class... Args>
T& widget<T>::emplace(Args&&... args) {
    static_assert(is_constructible_v<T, Args...>);
    static_assert(is_move_constructible_v<T> && !is_const_v<T>);
    value_ = T();
    static_assert(sizeof(T) > 0); // not part of the prefix; must be ignored
    return value_;
}

//! \mandates No other reference to `*this` is required during the call.
//! \effects Discards the contained value.
template <class T>
void widget<T>::shrink() {
    // The alias does not block derivation; authored Mandates replaces its wording.
    using value_type = T;
    static_assert(is_move_constructible_v<T>);
    value_ = T();
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_MANDATES_HPP
