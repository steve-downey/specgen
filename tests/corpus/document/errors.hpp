// tests/corpus/document/errors.hpp                                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_DOCUMENT_ERRORS_HPP
#define BEMAN_SPECGEN_CORPUS_DOCUMENT_ERRORS_HPP

namespace demo {

// \ref{span.errors}, error types

//! \remarks The enumerators have the following meanings:
//! \item `bad_input` -- the input is not what the encoding allows.
//! \item `truncated` -- the input ends in the middle of a sequence.
enum class error_kind { bad_input, truncated };

// A variable template used at several arguments instantiates once per
// argument, and each instantiation reports the template's own location: they
// are the same declaration, and a synopsis lists it once.
//! \remarks `flag<T>` is `true` for a type the encoding admits.
template <class T>
inline constexpr bool flag = false;

inline constexpr bool flag_int  = flag<int>;
inline constexpr bool flag_char = flag<char>;

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_DOCUMENT_ERRORS_HPP
