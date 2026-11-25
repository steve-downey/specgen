// tests/corpus/spec_expos_constructors.hpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A constructor of an exposition-only class is named the way its class is
// (issue #434), as the draft writes `@\exposid{iterator}@(Parent& parent, ...)`
// in [range.transform.iterator]. A constructor's name has no type use behind
// it, so it used to keep the C++ name while the destructor beside it took the
// exposition name.
//
// `view::iterator` is a nested `\expos` class: its constructors and its
// destructor render as `$iterator$` in its itemdecls. `view::sentinel` is
// shown in full, so its constructor renders inside its synopsis, under the
// `\expos(name)` spelling `end-mark`. `box_impl` is a namespace-scope `\expos`
// class template spelled `box`, where the set keys the template and the
// constructor and destructor name its pattern record; the destructor kept
// `~box_impl` too, and the constructor's itemdecl is rendered as well.
//
// Self-contained (no #includes), so it parses standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_CONSTRUCTORS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_CONSTRUCTORS_HPP

namespace demo {

//! \expos(box)
template <class T>
class box_impl {
  public:
    box_impl(int v);
    ~box_impl() = default;
};

//! \effects Holds `v`.
template <class T>
box_impl<T>::box_impl(int) {}

// \rSec2[demo.view]{Class `view`}

class view {
  public:
    //! \expos
    //! \seebelow
    class iterator {
      public:
        iterator();
        iterator(int handle);
        ~iterator();
    };
    //! \expos(end-mark)
    class sentinel {
      public:
        sentinel(int handle);
    };
};

//! \effects Value-initializes nothing.
inline view::iterator::iterator() {}

//! \effects Opens `handle`.
inline view::iterator::iterator(int) {}

//! \effects Closes the handle.
inline view::iterator::~iterator() {}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_CONSTRUCTORS_HPP
