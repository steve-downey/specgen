// tests/corpus/spec_nested_namespace.hpp                           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (design §3.5, issue #209): a nested namespace
// that is part of the specified interface renders as the draft writes one,
// instead of flattening into `namespace std` beside everything else.
//
// `//! \specified` on a namespace is what says so. The header synopsis then
// opens a `namespace views { ... }` block for the declarations in it, the
// [ranges.syn] shape, and closes it when source order leaves the namespace;
// a class defined in one encloses as `namespace std::proto {`, the
// [fs.class.path] shape; and a function's or variable's itemdecl qualifies
// its declarator, `proto::decode`, the way [alg.copy] writes `ranges::copy`.
// A type's itemdecl -- the enumeration here -- stays bare, as the grammar
// requires and the draft writes them.
//
// `proto::decode` beside `decode` one level up is the hazard that motivated
// the facility: flattened, the two were siblings in `namespace std`, which is
// not the interface and, for a variable template beside a function template
// of the same name, not valid C++ either.
//
// The qualifier `views::` written in code survives the mapping and is *not* a
// leak: a specified namespace is neither dropped nor foreign, so this header
// validates clean. Written in full, `demo::views::adapt` loses its
// `demo::` prefix through the ordinary prefix walk and renders the same way.
// `detail` stays what it was -- unmarked, its declaration `\omit`ted, and its
// qualifier masked away -- and a `std::ranges` specialization renders inside
// `namespace ranges {` with no marker at all, the standard's own vocabulary
// being specified by definition.
//
// The `std` declarations are local stand-ins: the corpus is hermetic (no
// #includes), and only the resolution matters. Self-contained under
// -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_NESTED_NAMESPACE_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_NESTED_NAMESPACE_HPP

namespace std {
namespace ranges {
template <class T>
inline constexpr bool enable_borrowed_range = false;
} // namespace ranges
} // namespace std

namespace demo {

// \rSec2[demo.syn]{Header <demo> synopsis}

// \ref{demo.buffer}, class template buffer
template <class T>
class buffer;

namespace detail {
//! \omit
struct adapt_fn {};
} // namespace detail

// \ref{demo.views}, range adaptor objects
//! \specified
namespace views {
//! \seebelow
//! \remarks The name `views::adapt` denotes a range adaptor object. Given a
//! subexpression `E`, the expression `views::adapt(E)` is
//! expression-equivalent to `buffer(E)`.
inline constexpr detail::adapt_fn adapt{};
} // namespace views

//! \specified
namespace proto {

// \ref{demo.encoding}, encodings
//! \remarks The values name the protocol families a decoder reads.
enum class encoding { utf8, utf16 };

// \ref{demo.decoder}, class template decoder
template <encoding E>
class decoder;

// \ref{demo.decode}, decoding
template <encoding E>
decoder<E> decode(const char* text);

} // namespace proto

// \ref{demo.decode}, decoding
template <class D>
D decode(D d);

template <class T>
inline constexpr bool adapts = sizeof(views::adapt) == sizeof(T);

template <class T>
inline constexpr bool adapts_qualified = sizeof(demo::views::adapt) == sizeof(T);

} // namespace demo

namespace std::ranges {
template <class T>
inline constexpr bool enable_borrowed_range<demo::buffer<T>> = true;
} // namespace std::ranges

/// END [demo.syn]

namespace demo {

// \rSec2[demo.buffer]{Class template `buffer`}

template <class T>
class buffer {
  public:
    // \ref{demo.buffer.obs}, observers
    constexpr bool empty() const;
};

// \rSec3[demo.buffer.obs]{Observers}

//! \returns `true`.
template <class T>
constexpr bool buffer<T>::empty() const {
    return true;
}

// \rSec2[demo.views]{Range adaptor objects}

// \rSec2[demo.encoding]{Encodings}

// \rSec2[demo.decoder]{Class template `decoder`}

// The namespace is reopened without the marker: one redeclaration carrying
// it is enough.
namespace proto {

template <encoding E>
class decoder {
  public:
    // \ref{demo.decoder.obs}, observers
    constexpr encoding scheme() const;
};

// \rSec3[demo.decoder.obs]{Observers}

//! \returns `E`.
template <encoding E>
constexpr encoding decoder<E>::scheme() const {
    return E;
}

} // namespace proto

// \rSec2[demo.decode]{Decoding}

namespace proto {

//! \returns A `decoder<E>` over `text`.
template <encoding E>
decoder<E> decode(const char* text) {
    return decoder<E>{};
}

} // namespace proto

//! \returns `d`.
template <class D>
D decode(D d) {
    return d;
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_NESTED_NAMESPACE_HPP
