// tests/corpus/spec_friend_redeclaration.hpp                      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A namespace-scope function that a class also declares as a friend, so it can
// reach a private constructor, is specified where it is declared (issue #441).
// Its synopsis line and its item declaration come from the namespace-scope
// declaration, with no `friend`, and the friend declaration contributes
// nothing: no line in the class synopsis, no entry in the class's coverage
// roster. It used to be keyed to the friend, so the item said `friend`, the
// namespace-scope line was missing, and validation reported the function as
// undescribed.
//
// `make_thing` is befriended under `\omit`, `make_other` with no marker; both
// render alike. `swap` is a hidden friend, declared nowhere else, and keeps
// its friend declaration as before.
//
// Self-contained (no #includes), so it parses standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_FRIEND_REDECLARATION_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_FRIEND_REDECLARATION_HPP

// \rSec2[demo.syn]{Header `<demo>` synopsis}

namespace demo {
// \ref{demo.thing}, thing

//! \at demo.thing
//! \remarks A thing.
class thing {
    //! \omit
    explicit thing(int v) noexcept;

  public:
    // \ref{demo.thing}, construction
    thing() noexcept;
    //! \omit
    friend thing make_thing(int v);
    friend thing make_other(int v);

    // \ref{demo.thing}, swap
    //! \effects Exchanges nothing.
    friend void swap(thing&, thing&) noexcept {}
};

// \ref{demo.thing}, creating a thing
thing make_thing(int v);
thing make_other(int v);
} // namespace demo

/// END [demo.syn]

// \rSec2[demo.thing]{Things}

namespace demo {
//! \effects Constructs a default thing.
inline thing::thing() noexcept = default;

//! \omit
inline thing::thing(int) noexcept {}

//! \returns A thing made from `v`.
inline thing make_thing(int v) { return thing{v}; }

//! \returns Another thing made from `v`.
inline thing make_other(int v) { return thing{v}; }
} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_FRIEND_REDECLARATION_HPP
