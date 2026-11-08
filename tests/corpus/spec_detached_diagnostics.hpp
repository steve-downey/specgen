// tests/corpus/spec_detached_diagnostics.hpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_DETACHED_DIAGNOSTICS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_DETACHED_DIAGNOSTICS_HPP

namespace demo {

// \rSec3[detached.general]{General}

//! Detached prose must report its location.

// \rSec3[detached.other]{Other wording}

/*! Detached block-form prose must report too. */

// A draft-form prefix coalesced into the markup block.
//! \unknown-detached

// \rSec3[detached.labelled]{Detached labelled wording}

//! \remarks This description has no declaration.

// \rSec3[detached.attached]{Attached wording}

//! \unknown-attached
//! \effects Attached findings are reported once.
inline void attached() {}

//! \omit

} // namespace demo

// \rSec3[detached.syn]{Header synopsis}
#include "specgen_detached/part.hpp"
/// END [detached.syn]

#endif // BEMAN_SPECGEN_CORPUS_SPEC_DETACHED_DIAGNOSTICS_HPP
