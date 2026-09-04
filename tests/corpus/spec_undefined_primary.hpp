// tests/corpus/spec_undefined_primary.hpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A documented record declaration the header never defines is an ordinary
// wording item — an undefined class-template primary is the normal way to
// write an algebra whose operations a model must register. A forward
// declaration of a class defined below, an undocumented never-defined
// declaration, and an `\omit`ted one all contribute nothing (and never an
// empty code block).

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_UNDEFINED_PRIMARY_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_UNDEFINED_PRIMARY_HPP

namespace demo {

struct registry;

class never_defined;

// \rSec2[undef.primary]{Undefined primaries}

//! \remarks A program may specialize `join` for cv-unqualified
//! program-defined types.
template <class L, class R>
struct join;

//! \remarks Registration points for the bottom element.
template <class T>
struct bottom;

//! \also
template <class T>
struct unit;

//! \remarks An undefined primary with a non-template head.
struct bottom_tag;

//! \omit
template <class T>
struct erased;

struct registry {};

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_UNDEFINED_PRIMARY_HPP
