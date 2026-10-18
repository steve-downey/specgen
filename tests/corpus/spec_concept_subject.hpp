// tests/corpus/spec_concept_subject.hpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The subject of a derived "`X` models `C`" conjunct (design §5.1, issue
// #126). A concept-id with one template argument fronts that argument as the
// subject, and the argument used to be copied from source as written: a
// `std::` qualifier that every other rendering drops survived, an
// exposition-only alias kept its implementation name, and an east-const
// `T const&` kept the order the draft spells `const T&`. The subject is now
// rewritten like the value form beside it, which always dropped the
// qualifier. This header is west-const like every corpus header; its
// `.eastconst` golden sibling flips `const T&` and requires the same output.
//
// `g` shows it in a Constraints element and `h` in a Mandates element lifted
// from a static_assert, the other place a conjunct is phrased. The `std`
// stand-ins keep the header hermetic; only their resolution matters, and as
// undocumented alias and variable templates they render nothing themselves.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_CONCEPT_SUBJECT_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_CONCEPT_SUBJECT_HPP

namespace std {
template <class T>
using remove_cvref_t = T;
template <class T>
inline constexpr bool is_integral_v = true;
} // namespace std

namespace demo {

template <class T>
concept small = sizeof(T) < 8;

namespace detail {
//! \expos
template <class T>
using inner_t = T;
} // namespace detail

//! \effects Does nothing.
template <class T>
    requires small<typename std::remove_cvref_t<T>::value_type> && small<detail::inner_t<T>> && small<const T&> &&
             std::is_integral_v<std::remove_cvref_t<T>>
void g(T&&) {}

//! \effects Does nothing.
template <class T>
void h(T&&) {
    static_assert(small<std::remove_cvref_t<T>>);
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_CONCEPT_SUBJECT_HPP
