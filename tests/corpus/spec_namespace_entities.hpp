// tests/corpus/spec_namespace_entities.hpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Documented namespace-scope aliases, alias templates, variables, variable
// templates, concepts, and enumerations are ordinary wording items. Adjacent
// aliases group with \also and alias masking applies exactly as in a class
// body; unannotated and `\omit`ted entities stay absent.
//
// An enumeration is a type whose definition is its interface (issue #68), so
// its declaration is the itemdecl and the docblock describes what the
// enumerators mean -- scoped or not, with an enum-base or without.  Marked
// `\expos` it is a standalone exposition-only synopsis like the other kinds,
// and every use of it -- its type, and a qualified enumerator -- renders under
// the exposition name.
//
// Masking reaches the kinds whose *definition* is the implementation, not just
// the ones whose declared type is (issue #50): bare `\seebelow` writes an
// alias's right-hand side and a concept's constraint-expression as *see
// below*, and it composes with `\expos` rather than being displaced by it --
// an exposition-only entity whose definition is not specified. Without the
// mask an exposition-only alias must render its whole definition, so every
// entity that definition names has to be exposed too, and the chain does not
// always terminate usefully.

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

//! \remarks The concept is specified below.
//! \seebelow
template <class T>
concept masked = requires { typename detail::opaque_token; };

//! \expos
//! \seebelow
template <class T>
using hidden_t = detail::opaque_token*;

//! \expos
//! \seebelow
template <class T>
concept hidden_usable = requires { typename detail::opaque_token; };

//! \remarks The enumerators have the following meanings:
//! \item `red` -- the default channel.
//! \item `green` -- the channel a program selects.
enum class color { red, green };

//! \remarks The width of a link, in bytes.
enum link_width : unsigned char { narrow = 1, wide = 2 };

//! \expos
enum class channel_state { open, closed };

//! \remarks The state a new channel starts in.
inline constexpr channel_state initial_state = channel_state::open;

//! \omit
inline constexpr int scratch_count = 3;

//! \omit
enum class scratch_kind { alpha };

using unannotated_helper = void;

enum class unannotated_kind { beta };

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_NAMESPACE_ENTITIES_HPP
