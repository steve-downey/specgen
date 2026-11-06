// tests/corpus/spec_description_blocks.hpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_DESCRIPTION_BLOCKS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_DESCRIPTION_BLOCKS_HPP

namespace demo {

// \rSec3[blocks.general]{Description blocks}

//! \effects Performs the operation.
//! \note An explanation with `int` and \iref{blocks.general}.
//!
//! A second informative paragraph.
//! \item The informative list is unnumbered.
//! \endnote
//! \example The following code illustrates an operation:
//! \codeblock
//!   run();
//!
//! // Commands in a code payload remain literal.
//! // \effects is part of this comment.
//! \endcodeblock
//! The example continues after its code.
//! \endexample
//! This continues the normative paragraph.
//!
//! This is a separate normative paragraph.
inline void run() {}

//! The result is equal to:
//! \codeblock
//! 1 + 2
//! \endcodeblock
//! Otherwise, the result is zero.
inline int value() { return 3; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_DESCRIPTION_BLOCKS_HPP
