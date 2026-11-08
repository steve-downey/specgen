// tests/corpus/spec_resume_description.hpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_RESUME_DESCRIPTION_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_RESUME_DESCRIPTION_HPP

namespace demo {

// \rSec3[resume.assign]{Assignment}

//! \ensures The stored value is one of:
//! \item the argument, if a value was supplied;
//! this continuation belongs to the first item.
//!
//! \item the previous value, otherwise.
//!
//! Otherwise, the object is unchanged.
//!
//! A separate postcondition paragraph.
//! \item One further guarantee.
//!
//! This continues the further guarantee.
//! \remarks A following element remains separate.
inline void assign() {}

// \rSec3[resume.tables]{Tables}

//! \effects The following table applies.
//! \libtab2[resume.flat]{Flat table}
//! \column Condition
//! \column Effect
//! \row A
//! \cell B
//! \endlibtab2
//! Prose after the flat table.
//! \lib2dtab2[resume.cross]{Cross table}
//! \column First
//! \column Second
//! \row Row
//! \cell C
//! \cell D
//! \endlib2dtab2
//! Prose after the cross table.
inline void apply() {}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_RESUME_DESCRIPTION_HPP
