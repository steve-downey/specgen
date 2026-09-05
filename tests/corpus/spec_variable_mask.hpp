// tests/corpus/spec_variable_mask.hpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Bare `\seebelow` on a namespace-scope variable (template) masks the
// declared type as the draft's *unspecified* placeholder and drops the
// initializer — the customization-point-object shape,
// `inline constexpr unspecified thing;` (issue #24). The mask keeps the
// `detail::` spelling out of the wording entirely, list-init and copy-init
// alike; the marker used to be accepted with no effect and no diagnostic.
//
// A reference is the spelling for an object that forwards to an
// implementation-namespace one, and the reference and its `const` are part of
// the declared type, so the mask takes them too (issue #33). It used to reach
// only the pointee's TypeLoc, leaving a stray `const` standing against the
// placeholder and eating the space before the name.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_VARIABLE_MASK_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_VARIABLE_MASK_HPP

namespace demo {

namespace detail {
struct adaptor {};
template <typename T>
struct tagger {};
inline constexpr adaptor instance{};
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

//! \seebelow
//! \remarks The name `forwarded` denotes an object forwarding to the
//!   implementation's own.
inline constexpr const detail::adaptor& forwarded = detail::instance;

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_VARIABLE_MASK_HPP
