// tests/corpus/spec_leading_description.hpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_LEADING_DESCRIPTION_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_LEADING_DESCRIPTION_HPP

namespace demo {

//! A wrapper stores one value.
//!
//! Its description is routed independently of its synopsis.
//! \at leading.general
struct wrapper {
    // \ref{leading.members}, members
    //! Constructs an empty wrapper.
    wrapper() = default;

    //! A concrete wrapper supplies this operation.
    virtual void run() = 0;
};

// \rSec2[leading]{Leading descriptions}
// \rSec3[leading.general]{General}
// \rSec3[leading.members]{Members}
// \rSec3[leading.functions]{Functions}

//! The first function has prose only.
inline void first() {}

//! The second function has its own description.
inline void second() {}

//! The third function offers these choices:
//! \item a first choice, or
//! \item a second choice.
//!
//! It introduces the labelled requirements.
//!
//! This is a separate leading paragraph with \iref{leading.general}.
//! \effects Performs the operation.
inline void third() {}

//! \also
inline void third(int) {}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_LEADING_DESCRIPTION_HPP
