// tests/corpus/spec_nested_items.hpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A list inside a list item, written in a docblock (issue #390): an `\item`
// indented past the `\item` before it opens a list inside that item, as a
// `\codeblock` indented under an item is that item's (issue #367). One
// indented as the outer items are goes back to their list.
//
// Self-contained (no #includes), so it parses standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_NESTED_ITEMS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_NESTED_ITEMS_HPP

namespace demo {

// \rSec2[demo.wait]{Waiting}

//! \effects Blocks until one of:
//! \item a value is ready, and then either
//!   \item it is returned, or
//!   \item it is discarded, equivalent to:
//!     \codeblock
//!     discard();
//!     \endcodeblock
//! \item the timeout expires.
inline void wait() {}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_NESTED_ITEMS_HPP
