// tests/corpus/spec_nested_namespaces.hpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// \rSec2[nested.syn]{Header <nested> synopsis}

namespace demo {

namespace detail {
//! \expos
struct adaptor {};
} // namespace detail

//! \specified
namespace views {

// \ref{nested.adaptor}, range adaptor objects
//! \seebelow
//! \effects Returns a null-terminated view of `r`.
inline constexpr detail::adaptor null_term{};

} // namespace views

//! \specified
namespace whatwg {

enum class encoding { utf8 };

// \ref{nested.whatwg}, WHATWG operations
//! \remarks `E` identifies an encoding.
template <encoding E>
inline constexpr bool decode = true;

namespace detail {
//! \expos
struct state {};
} // namespace detail

} // namespace whatwg

// \ref{nested.decode}, decoding
//! \returns `true`.
template <class D>
constexpr bool decode(D) {
    return true;
}

} // namespace demo

/// END [nested.syn]

// \rSec3[nested.adaptor]{Range adaptor objects}

// \rSec3[nested.decode]{Decoding}

// \rSec3[nested.whatwg]{WHATWG operations}
