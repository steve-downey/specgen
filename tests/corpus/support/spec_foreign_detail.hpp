// tests/corpus/support/spec_foreign_detail.hpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Support header for spec_foreign_include.hpp (design §9): the
// implementation machinery a real library keeps in a detail/ header, plus a
// local std::ranges stand-in (the corpus is hermetic, so the real header is
// never included; only the resolution matters). Everything here is *declared
// outside the main file* on purpose — that placement is what the qualifier
// half of the leakage checker must see through.
//
// `steppable` is the same placement asked the other way (issue #36): an
// exposition-only helper marked here and named by a requires-clause in the
// main file. Its `\expos` must be honoured across the include, or the tidy
// refactor that moved it here would cost the author a rename or a move out
// of `detail` — an edit to the library, made to suit the generator.

#ifndef BEMAN_SPECGEN_CORPUS_SUPPORT_SPEC_FOREIGN_DETAIL_HPP
#define BEMAN_SPECGEN_CORPUS_SUPPORT_SPEC_FOREIGN_DETAIL_HPP

namespace std {
namespace ranges {
template <class T>
using probe_t = T;
} // namespace ranges
} // namespace std

namespace demo {
namespace detail {
struct evaluator {};
inline constexpr evaluator eval{};

//! \expos
template <class T>
concept steppable = requires(const T& impl) { impl.step(eval); };
} // namespace detail
} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SUPPORT_SPEC_FOREIGN_DETAIL_HPP
