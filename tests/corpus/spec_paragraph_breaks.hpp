// tests/corpus/spec_paragraph_breaks.hpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_PARAGRAPH_BREAKS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_PARAGRAPH_BREAKS_HPP

namespace demo {

// \rSec3[paragraphs.general]{General}

//! The header provides these choices:
//! \item the first choice, or
//! \item the second choice.
//!
//!
//! Each choice has these properties:
//! \item the first property, and
//! \item the second property.
//!
//! This continues the second paragraph.

// \rSec3[paragraphs.operations]{Operations}

//! Leading wording has a list:
//! \item the first entry.
//!
//!
//! Another leading paragraph has its own list:
//! \item the second entry.
//! \effects The operation has these effects:
//! \item the first effect.
//!
//!
//! A separate paragraph has these effects:
//! \item the second effect.
//!
//! This continues the second Effects paragraph.
//! \remarks The table describes the choices:
//! \libtab2[paragraphs.choices]{Choices}
//! \column Choice
//! \column Meaning
//! \row First
//! \cell The first choice.
//! \endlibtab2
//!
//!
//! This starts a new paragraph after the flat table.
//! \lib2dtab2[paragraphs.properties]{Properties}
//! \column First property
//! \column Second property
//! \row First choice
//! \cell The first property applies.
//! \cell The second property applies.
//! \endlib2dtab2
//!
//!
//! This starts a new paragraph after the two-dimensional table.
inline void operate() {}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_PARAGRAPH_BREAKS_HPP
