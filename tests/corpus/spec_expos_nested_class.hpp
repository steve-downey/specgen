// tests/corpus/spec_expos_nested_class.hpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// `\expos` on a *nested* class (issue #80).  A view's iterator is one: private,
// and named by every `begin`, `end` and `operator++` signature the class
// publishes.  Unmarked it is dropped as an ordinary private member while the
// wording goes on naming it -- a name the reader cannot see, reported by
// nothing.
//
// Marked `\expos` it renders under its exposition name, definition and all.
// Marked bare `\seebelow` as well it renders as a declaration, which is the
// shape [range.transform.view] prints: the class is specified in its own
// subclause, and its state is not the synopsis's business.  Its members are
// exposition too, so an extracted body that names one says the exposition
// name.  `state` is the other half of the marker: `\expos` alone keeps the
// definition, which is what an exposition-only helper with nothing to specify
// looks like.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_NESTED_CLASS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_NESTED_CLASS_HPP

namespace demo {

// \rSec2[demo.view]{Class template `demo_view`}

template <class R>
class demo_view {
    //! \expos
    R base_;

    //! \expos
    //! \seebelow
    class iterator {
        //! \expos
        int value_ = 0;

      public:
        // \ref{demo.view.iterator}, iterator operations
        constexpr int operator*() const;
    };

    //! \expos
    struct state {
        bool started = false;
    };

  public:
    // \ref{demo.view}, access
    constexpr iterator begin() const;
};

//! \returns An iterator over the base range, positioned at its first element.
template <class R>
constexpr typename demo_view<R>::iterator demo_view<R>::begin() const {
    return iterator{};
}

// \rSec3[demo.view.iterator]{Class `demo_view::iterator`}

//! \returns-equiv
template <class R>
constexpr int demo_view<R>::iterator::operator*() const {
    return value_;
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_NESTED_CLASS_HPP
