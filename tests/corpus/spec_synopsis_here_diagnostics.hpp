// tests/corpus/spec_synopsis_here_diagnostics.hpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// `\synopsis-here` misplaced (issue #292). It divides the leading paragraphs
// of a class's docblock, so each of these is reported rather than ignored:
// after a labelled element, where nothing is left to divide; a second time;
// with text after it; and on a function, which has no synopsis of its own.
//
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_SYNOPSIS_HERE_DIAGNOSTICS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_SYNOPSIS_HERE_DIAGNOSTICS_HPP

namespace demo {

// \rSec2[demo.misplaced]{Misplaced markers}

//! \remarks The marker follows an element.
//! \synopsis-here
class after_element {};

//! The marker is written twice.
//! \synopsis-here
//! \synopsis-here
class twice {};

//! The marker takes no argument.
//! \synopsis-here please
class with_text {};

//! A function has no synopsis.
//! \synopsis-here
//! \effects Nothing.
inline void on_a_function() {}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_SYNOPSIS_HERE_DIAGNOSTICS_HPP
