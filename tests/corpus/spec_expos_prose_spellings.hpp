// tests/corpus/spec_expos_prose_spellings.hpp                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Three spellings in an authored code span that name an exposition-only
// member's exposition name without being a use of the member (issue #432).
//
// `begin()` writes the names in the mpark form already, `$iterator$` and
// `$codec$`; each renders as the one exposition name, not wrapped twice.
//
// `end()` names `demo::decoder<int>`, the namespace-scope class template,
// while `decoder_` is an exposition-only member of `view`. A name reached
// through `::`, or an exposition name followed by `<`, is not the member.
//
// `iterator::operator++` names `end()`, the enclosing view's member function,
// while `end_` is an exposition-only member of the iterator. A nested class's
// wording can name its enclosing class's members, so `end` is taken.
//
// Self-contained (no #includes), so it parses standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_PROSE_SPELLINGS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_PROSE_SPELLINGS_HPP

namespace demo {
template <class T>
class decoder {};

// \rSec2[demo.view]{Class `view`}

class view {
    //! \expos
    int codec_;
    //! \expos
    decoder<int> decoder_;

  public:
    //! \expos
    //! \seebelow
    class iterator {
        //! \expos
        int end_;

      public:
        iterator& operator++();
    };
    iterator begin();
    iterator end();
};

//! \returns An `$iterator$` over a copy of `$codec$`.
inline view::iterator view::begin() { return {}; }

//! \returns An `iterator` behaving as `demo::decoder<int>` does, and `decoder<int>` too.
inline view::iterator view::end() { return {}; }

//! \effects Makes `*this` equal to `end()`, through `end_`.
//! \returns `*this`.
inline view::iterator& view::iterator::operator++() { return *this; }
} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_PROSE_SPELLINGS_HPP
