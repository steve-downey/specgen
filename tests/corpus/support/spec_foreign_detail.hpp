// tests/corpus/support/spec_foreign_detail.hpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Support header for spec_foreign_include.hpp (design §9): the
// implementation machinery a real library keeps in a detail/ header, plus a
// local std::ranges stand-in (the corpus is hermetic, so the real header is
// never included; only the resolution matters). Everything here is *declared
// outside the main file* on purpose — that placement is what the qualifier
// half of the leakage checker must see through.

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
} // namespace detail
} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SUPPORT_SPEC_FOREIGN_DETAIL_HPP
