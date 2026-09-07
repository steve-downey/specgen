// tests/corpus/spec_variable_mask_diagnostics.hpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The targeted `\seebelow` forms have no meaning on a variable — there is no
// conditional noexcept or explicit to mask — so one is an Error rather than
// the silent no-op it used to be (issue #24). Marked `\expos` as well, the
// same form reaches the standalone-synopsis path instead, which read no
// marker at all and so reported nothing (issue #38).
//
// An enumeration accepts no `\seebelow` at all (issue #68): a variable masks
// its type, an alias and a concept their definitions, and the draft never
// spells an enum-base or an enumerator list *see below*. Silently ignoring the
// marker left a description claiming the synopsis beside it does not say what
// it plainly does.

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

//! \seebelow
//! \remarks The underlying type is unspecified.
enum class wrong_kind : unsigned char { a, b };

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_VARIABLE_MASK_DIAGNOSTICS_HPP
