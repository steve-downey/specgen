// tests/corpus/spec_authored_header_synopsis.hpp                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A header synopsis that is *authored*: a detached `\verbatim-synopsis` in a
// section whose stable name ends in `.syn`, which is the name the draft gives
// a header synopsis (issue #284). A gathered region is not a substitute for a
// header whose content is classes: it folds each class's whole synopsis into
// the header synopsis, where the draft's [expected.syn] has
// `template<class T, class E> class expected;` and the class synopsis is in
// the class's own subclause.
//
// The `.syn` name used to turn on the gatherer regardless, with two effects
// the same block in a section of any other name does not have:
//
//  - the block was treated as gathered content and wrapped in a second
//    `namespace std {`, although a detached block is the author's whole
//    synopsis, namespace included;
//  - the gatherer consumed `/// END [u.syn]`, so the section stayed open and
//    the exposition-only helper after the fence landed in its fragment.
//
// A region holding an authored synopsis and nothing gathered is now the
// section it would be under any other name: the block is left exactly as
// written, and the fence closes the section, so `helper_v` returns to the
// document root.
//
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_AUTHORED_HEADER_SYNOPSIS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_AUTHORED_HEADER_SYNOPSIS_HPP

namespace demo {

// \rSec2[u.syn]{Header synopsis}

//! \verbatim-synopsis
//! // mostly freestanding
//! namespace std {
//!   // \ref{u.thing}, class template thing
//!   template<class E> class thing;
//! }

/// END [u.syn]

//! \expos
template <class T>
inline constexpr bool helper_v = false;

// \rSec2[u.thing]{Class template thing}

template <class E>
class thing {};

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_AUTHORED_HEADER_SYNOPSIS_HPP
