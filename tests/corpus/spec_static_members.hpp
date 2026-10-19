// tests/corpus/spec_static_members.hpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A documented static data member is an item of its own (issue #148), as the
// draft gives `atomic<T>::is_always_lock_free` and the `numeric_limits`
// members: an itemdecl and the elements its docblock carries, routed by its
// class's `\ref` group or an `\at`, like an in-class alias.
//
// - `consumes` keeps its initializer in the itemdecl.
// - `digits` carries bare `\seebelow`, which drops the initializer from the
//   itemdecl and leaves its type alone; the synopsis keeps it.
// - `accepts` is a static data member template.
// - `version` is undocumented: its declaration is its specification.
// - `limit` is placed by `\at` in another section.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_STATIC_MEMBERS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_STATIC_MEMBERS_HPP

namespace demo {

template <class T>
class gauge {
  public:
    // \ref{gauge.members}, static members
    //! \remarks `true` when a gauge consumes its argument.
    static constexpr bool consumes = true;

    //! \seebelow
    //! \remarks The number of significant bits in a reading.
    static constexpr int digits = 8 * sizeof(T);

    //! \remarks `true` when a reading converts to `U`.
    template <class U>
    static constexpr bool accepts = sizeof(U) >= sizeof(T);

    static constexpr int version = 1;

    //! \at gauge.limits
    //! \remarks The largest reading.
    static constexpr int limit = 100;

    // \ref{gauge.ops}, operations
    constexpr T read() const;
};

// \rSec2[gauge]{Class template gauge}

// \rSec3[gauge.members]{Static members}

// \rSec3[gauge.limits]{Limits}

// \rSec3[gauge.ops]{Operations}

//! \returns A default reading.
template <class T>
constexpr T gauge<T>::read() const {
    return T();
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_STATIC_MEMBERS_HPP
