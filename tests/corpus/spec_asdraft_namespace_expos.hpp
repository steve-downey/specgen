// tests/corpus/spec_asdraft_namespace_expos.hpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// `\asdraft` on an exposition-only concept at namespace scope (issue #332).
// Its description is written beside its synopsis and is no item, so it pairs
// by the synopsis's declaration with the draft's item for
// *boolean-testable-impl*, an `itemdecl` with no `itemdescr`, and inherits the
// numbered paragraphs that follow it, up to the next declaration
// (tests/corpus/standard-slices/concept.booleantestable.slice).
//
// Self-contained (no #includes): `convertible_to` is a stand-in, `\omit`ted.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_ASDRAFT_NAMESPACE_EXPOS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_ASDRAFT_NAMESPACE_EXPOS_HPP

namespace demo {

//! \omit
template <class From, class To>
concept convertible_to = true;

// \rSec2[concept.booleantestable]{Boolean testability}

//! \asdraft
//! \expos
template <class T>
concept boolean_testable_impl = convertible_to<T, bool>;

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_ASDRAFT_NAMESPACE_EXPOS_HPP
