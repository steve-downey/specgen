// tests/corpus/spec_undocumented_definitions.hpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Out-of-line definitions that contribute nothing to the wording (issue #433).
//
// `hidden` is `\omit`ted, so its members' definitions are left out with it,
// the documented `hidden::g` included. `shown::iterator`'s constructor is a
// private member of an exposition-only class and has no docblock, and nothing
// precedes it for it to join as an overload. Both used to come out as one
// stray item between `shown`'s synopsis and its Remarks, holding every
// signature and no description, in the fenced header synopsis below and in
// an ordinary section alike.
//
// Self-contained (no #includes), so it parses standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_UNDOCUMENTED_DEFINITIONS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_UNDOCUMENTED_DEFINITIONS_HPP

// \rSec2[demo.syn]{Header `<demo>` synopsis}

namespace demo {
// \ref{demo.thing}, thing

//! \omit
template <class T>
class hidden {
  public:
    hidden(int);
    void f();
    void g();
};

//! \remarks A thing.
template <class T>
class shown {
  public:
    //! \expos
    //! \seebelow
    class iterator {
        iterator(int handle);
        friend class shown;
    };
    void g();
};

template <class T>
hidden<T>::hidden(int) {}
template <class T>
void hidden<T>::f() {}
//! \effects Does hidden g.
template <class T>
void hidden<T>::g() {}
template <class T>
shown<T>::iterator::iterator(int) {}

//! \effects Does g.
template <class T>
void shown<T>::g() {}
} // namespace demo

/// END [demo.syn]

// \rSec2[demo.thing]{Things}

namespace demo {
//! \omit
class plain_hidden {
  public:
    void f();
};

//! \remarks Another thing.
class plain_shown {
  public:
    //! \expos
    //! \seebelow
    class iterator {
        iterator(int handle);
        friend class plain_shown;
    };
    void g();
};

inline void plain_hidden::f() {}
inline plain_shown::iterator::iterator(int) {}

//! \effects Does plain g.
inline void plain_shown::g() {}
} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_UNDOCUMENTED_DEFINITIONS_HPP
