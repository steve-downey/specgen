// include/beman/specgen/frontend/build_failure.hpp               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// How a front-end entry point says it could not build what it was asked
// for. Below every entry point that can fail, so each can name it without
// reaching for build_document.hpp.

#ifndef BEMAN_SPECGEN_FRONTEND_BUILD_FAILURE_HPP
#define BEMAN_SPECGEN_FRONTEND_BUILD_FAILURE_HPP

#include <string>

namespace beman::specgen::frontend {

// build_document()'s hard-failure modes (decision expected-error-taxonomy: a
// single fallible step -> std::expected + and_then), two of them,
// deliberately worded apart. The first is the one collect_interleaved()
// shares: the header cannot be read, or Clang cannot build any AST from it
// at all. The second is different in
// kind, not degree: Clang *did* build an AST, but its own DiagnosticsEngine
// reports an error somewhere in the parse that produced it
// (parse_header's ParsedHeader::had_error, parse_header.hpp) — a header
// `generate` must not turn into wording, because extraction is lexical and a
// partial parse's visible symptom is not a missing type but a
// plausible-looking wrong one (an unresolved out-of-line definition reads as
// an unlinked, differently-qualified function). This is unrelated to, and
// does not touch, the docblock findings
// `generate` prints alongside a *successful* build (it still exits 0 on
// those): a docblock Error is markup the tool read correctly and disagreed
// with, while this failure means the tool did not read the code at all.
// `message` is ready to print as-is either way (main() does not need to
// reconstruct it).
struct BuildFailure {
    std::string message;
};

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_BUILD_FAILURE_HPP
