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

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_DOCUMENT_ERRORS_HPP
