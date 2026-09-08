// tests/corpus/spec_libtab2.hpp                                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// An enumeration's meanings table (issue #74): a flat two-column table, one
// row per enumerator, no row-heading column -- the shape
// [fs.enum.file.type] and its neighbours use, and the one \lib2dtab2 cannot
// represent without a wasted third column or a row heading pressed into
// service as the constant.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_LIBTAB2_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_LIBTAB2_HPP

namespace demo {

// \rSec2[demo.errors]{Errors}

//! \remarks The enumerators have the meanings in the following table.
//! \libtab2[demo.errors.tab]{Enum class `whatwg_error`}
//! \column Constant
//! \column Meaning
//! \row `invalid_byte`
//! \cell the input holds a byte the encoding does not allow in that
//! position.
//! \row `truncated_sequence`
//! \cell the input ends in the middle of a sequence.
//! \endlibtab2
enum class whatwg_error { invalid_byte, truncated_sequence };

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_LIBTAB2_HPP
