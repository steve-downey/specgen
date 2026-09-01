// tests/corpus/spec_verbatim.hpp                                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A standalone terminal verbatim-synopsis docblock (design §4.3).
// The payload deliberately names declarations that are not part of this
// translation unit: it is draft synopsis text, not C++ for Clang to parse.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_VERBATIM_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_VERBATIM_HPP

//! \verbatim-synopsis
//! namespace std {
//!   template<class T> struct hash;
//!   template<class T> struct hash<optional<T>>;
//! }

#endif // BEMAN_SPECGEN_CORPUS_SPEC_VERBATIM_HPP
