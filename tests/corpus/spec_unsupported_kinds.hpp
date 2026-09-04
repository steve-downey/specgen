// tests/corpus/spec_unsupported_kinds.hpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A docblock on an entity kind that produces no wording is an Error rather
// than a silent drop: the enum's description would vanish, and a function
// declaration's markup belongs at the definition. `\omit` and `\expos` are
// the author asking for silence, and an unannotated entity was never
// documented at all — those stay quiet.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_UNSUPPORTED_KINDS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_UNSUPPORTED_KINDS_HPP

namespace demo {

//! \remarks A scoped enumeration.
enum class color { red, green };

//! \returns The value unchanged.
int declared_only(int value);

//! \expos
template <class T>
using traverse_context_t = T;

//! \omit
enum class scratch { alpha };

enum class unannotated { beta };

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_UNSUPPORTED_KINDS_HPP
