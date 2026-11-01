// tests/corpus/spec_specified_diagnostics.hpp                      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// `\specified` (design §3.5, issue #209) misplaced, each shape an Error
// rather than a marker that silently takes no effect. The marker belongs on
// a namespace and nothing else; a namespace's docblock carries the marker
// and nothing else, a namespace having no wording of its own.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_SPECIFIED_DIAGNOSTICS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_SPECIFIED_DIAGNOSTICS_HPP

namespace demo {

// \rSec2[demo.misc]{Miscellany}

// The marker on a function definition, where no namespace is being named.
//! \specified
//! \returns `0`.
inline int zero() { return 0; }

// Another marker beside it on a namespace: `\omit` means nothing there.
//! \specified
//! \omit
namespace detail {}

// Description text on a namespace: nothing renders it.
//! \specified
//! \remarks The names below are adaptor objects.
namespace views {}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_SPECIFIED_DIAGNOSTICS_HPP
