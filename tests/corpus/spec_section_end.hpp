// tests/corpus/spec_section_end.hpp                                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// An ordinary section can be closed explicitly so declarations between two
// clauses return to the document root. The exposition-only helper below is
// part of the generated root fragment, not either neighboring class clause.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_SECTION_END_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_SECTION_END_HPP

namespace demo {

// \rSec2[demo.box]{Class template `box`}

//! \remarks A box.
template <class T>
class box {};

/// END [demo.box]

namespace detail {

//! \expos
template <class T>
inline constexpr bool is_box_v = false;

} // namespace detail

// \rSec2[demo.crate]{Class template `crate`}

//! \remarks A crate.
template <class T>
class crate {};

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_SECTION_END_HPP
