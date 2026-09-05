// tests/corpus/spec_variable_mask_diagnostics.hpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The targeted `\seebelow` forms have no meaning on a variable — there is no
// conditional noexcept or explicit to mask — so one is an Error rather than
// the silent no-op it used to be (issue #24). Marked `\expos` as well, the
// same form reaches the standalone-synopsis path instead, which read no
// marker at all and so reported nothing (issue #38).

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_VARIABLE_MASK_DIAGNOSTICS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_VARIABLE_MASK_DIAGNOSTICS_HPP

namespace demo {

namespace detail {
struct adaptor {};
} // namespace detail

//! \seebelow noexcept
inline constexpr detail::adaptor wrong{};

//! \expos
//! \seebelow explicit
inline constexpr detail::adaptor wrong_expos{};

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_VARIABLE_MASK_DIAGNOSTICS_HPP
