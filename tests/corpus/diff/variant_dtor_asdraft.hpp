// tests/corpus/diff/variant_dtor_asdraft.hpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// `\asdraft` on an item whose draft description holds mathematics the
// importer keeps as raw LaTeX, `$\tcode{T}_i$` in running text, which it
// reports as a warning (tests/corpus/standard-slices/variant.dtor.slice).
// Under --warnings-as-errors that warning fails the run (issue #372).

#ifndef BEMAN_SPECGEN_CORPUS_DIFF_VARIANT_DTOR_ASDRAFT_HPP
#define BEMAN_SPECGEN_CORPUS_DIFF_VARIANT_DTOR_ASDRAFT_HPP

// \rSec3[variant.dtor]{Destructor}

//! \asdraft
//! \verbatim-itemdecl
//! constexpr ~variant();

#endif // BEMAN_SPECGEN_CORPUS_DIFF_VARIANT_DTOR_ASDRAFT_HPP
