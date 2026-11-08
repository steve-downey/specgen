// tests/corpus/spec_section_prose.hpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_SECTION_PROSE_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_SECTION_PROSE_HPP

namespace demo {

// \rSec2[section]{Section prose}
// \rSec3[section.general]{General}

//! The header provides a `wrapper` for callable objects.
//!
//! A separate paragraph refers to \iref{section.class}.

// \rSec3[section.class]{Class template wrapper}

//! The `wrapper` class template stores a callable object.
template <class T>
struct wrapper {};

// \rSec3[section.operations]{Operations}

//! \effects Performs the first operation.
inline void first() {}

//! Wording between items stays between those items.

// A draft-form comment remains source scaffolding.

//! \effects Performs the second operation.
inline void second() {}

// \rSec4[section.operations.detail]{Details}

/*! A block-form paragraph belongs to the nested section. */

/// END [section.operations.detail]

//! This paragraph belongs to the enclosing section again.

} // namespace demo

// \rSec3[section.syn]{Header synopsis}
#include "specgen_section/part.hpp"
/// END [section.syn]

#endif // BEMAN_SPECGEN_CORPUS_SPEC_SECTION_PROSE_HPP
