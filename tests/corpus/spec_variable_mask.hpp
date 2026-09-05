// tests/corpus/spec_variable_mask.hpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Bare `\seebelow` on a namespace-scope variable (template) masks the
// declared type as the draft's *unspecified* placeholder and drops the
// initializer — the customization-point-object shape,
// `inline constexpr unspecified thing;` (issue #24). The mask keeps the
// `detail::` spelling out of the wording entirely, list-init and copy-init
// alike; the marker used to be accepted with no effect and no diagnostic.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_VARIABLE_MASK_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_VARIABLE_MASK_HPP

namespace demo {

namespace detail {
struct adaptor {};
template <typename T>
struct tagger {};
} // namespace detail

//! \seebelow
//! \remarks The name `thing` denotes a customization point object.
inline constexpr detail::adaptor thing{};

//! \seebelow
//! \remarks The copy-initialized spelling masks the same way.
inline constexpr detail::adaptor copied = detail::adaptor{};

//! \seebelow
//! \remarks The name `tag` denotes a family of tag objects.
template <typename T>
inline constexpr detail::tagger<T> tag{};

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_VARIABLE_MASK_HPP
