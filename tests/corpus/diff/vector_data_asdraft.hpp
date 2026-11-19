// tests/corpus/diff/vector_data_asdraft.hpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// `\asdraft` on an item whose draft description holds macros the importer
// keeps as raw LaTeX, `\range` and `\keyword`, which it reports as warnings
// (tests/corpus/standard-slices/vector.data.slice). Under
// --warnings-as-errors those warnings fail the run (issue #372).

#ifndef BEMAN_SPECGEN_CORPUS_DIFF_VECTOR_DATA_ASDRAFT_HPP
#define BEMAN_SPECGEN_CORPUS_DIFF_VECTOR_DATA_ASDRAFT_HPP

// \rSec3[vector.data]{Data}

//! \asdraft
//! \verbatim-itemdecl
//! constexpr T*       data() noexcept;
//! constexpr const T* data() const noexcept;

#endif // BEMAN_SPECGEN_CORPUS_DIFF_VECTOR_DATA_ASDRAFT_HPP
