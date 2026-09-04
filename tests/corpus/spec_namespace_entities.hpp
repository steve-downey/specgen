// tests/corpus/spec_namespace_entities.hpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Documented namespace-scope aliases, alias templates, variables, variable
// templates, and concepts are ordinary wording items. Adjacent aliases group
// with \also and alias masking applies exactly as in a class body;
// unannotated and `\omit`ted entities stay absent.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_NAMESPACE_ENTITIES_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_NAMESPACE_ENTITIES_HPP

namespace demo {
namespace detail {
struct opaque_token;
} // namespace detail

// \rSec2[demo.vocab]{Vocabulary}

//! \remarks The identity alias.
template <class T>
using id_t = T;

//! \remarks The value aliases.
using value_t = int;

//! \also
using size_type = unsigned long;

//! \remarks The token type is implementation-defined.
//! \impdef
using token_t = detail::opaque_token;

//! \remarks `flag_v<T>` is `false` unless a program specializes it.
template <class T>
inline constexpr bool flag_v = false;

//! \remarks The registration limit.
inline constexpr int max_links = 8;

//! \remarks A type is usable if it names a member type.
template <class T>
concept usable = requires { typename T::type; };

//! \omit
inline constexpr int scratch_count = 3;

using unannotated_helper = void;

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_NAMESPACE_ENTITIES_HPP
