// tests/corpus/spec_shared_spelling.hpp                            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (design §9: the leakage checker's bare-name
// half, issue #93). A documented enumeration's enumerator shares a spelling
// with a generated lookup table declared in an *included* header — the
// encoding names the enumerator and names the table, which is why a real
// library has both — and the table is reached only from a function body the
// tool never renders. The bare-name check resolves references, but reports
// them by text, so before decision shared-spelling-foreign-name the shared
// spelling drew findings on `windows_1252` at the enumeration's own
// declaration and on the qualified `codec::windows_1252` in its \remarks:
// the check reporting an enumerator as undocumented at the point where the
// enumeration that documents it is being rendered.
//
// This header keeps no leak: it must validate silently, which is what the
// `.validate` sibling of its generate golden demands. The reported name is
// declared by this run, so it is this run's to explain, and the table is
// never rendered at all.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_SHARED_SPELLING_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_SHARED_SPELLING_HPP

#include "support/spec_shared_spelling_tables.hpp"

namespace demo {

// \rSec2[demo.codec]{Encodings}

//! \remarks The enumerators have the following meanings:
//! \item `utf_8` -- the encoding a program gets by default.
//! \item `windows_1252` -- the encoding a legacy label selects.
enum class codec { utf_8, windows_1252 };

//! \returns The code unit at position `i` of the encoding `c`.
//! \remarks The labels of `codec::windows_1252` are the ones a legacy
//! document carries.
inline int decode(codec c, int i) { return c == codec::windows_1252 ? detail::tables::windows_1252[i] : i; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_SHARED_SPELLING_HPP
