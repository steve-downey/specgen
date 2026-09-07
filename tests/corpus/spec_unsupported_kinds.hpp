// tests/corpus/spec_unsupported_kinds.hpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A docblock on an entity kind that produces no wording is an Error rather
// than a silent drop: a namespace alias has no wording of its own, and a
// function declaration's markup belongs at the definition. `\omit` is the
// author asking for silence, and an unannotated entity was never documented
// at all — those stay quiet.
//
// An enumeration used to be the headline case here and is one no longer
// (issue #68): it is a documented namespace entity like any other, and
// `spec_namespace_entities.hpp` pins what it renders.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_UNSUPPORTED_KINDS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_UNSUPPORTED_KINDS_HPP

namespace demo {
namespace detail {
struct opaque_token;
} // namespace detail

//! \remarks The implementation namespace, in short.
namespace shorthand = detail;

//! \returns The value unchanged.
int declared_only(int value);

//! \omit
namespace scratch_ns = detail;

namespace unannotated_ns = detail;

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_UNSUPPORTED_KINDS_HPP
