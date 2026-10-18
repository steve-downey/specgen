// tests/corpus/spec_expos_plain_class.hpp                           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// `\expos` on a non-template class, and on using-declarations whose targets
// are non-template types (issue #142).
//
// `token` is a plain class marked directly. It used to be no exposition
// candidate at all, so the marker was ignored without a word and every use
// kept the real name. Its definition now renders like a marked class
// template's, and its uses -- a return type, a data member, a braced
// initializer in an equivalent-to body -- take its exposid.
//
// The three `detail` using-declarations name a class, an alias, and an
// enumeration declared in a support header. A use written through one is a
// UsingTypeLoc, which the rewrite had no hook for, so `detail::helper_t` stayed
// verbatim and validation reported `detail` as leakage; a variable template
// reached the same way always worked, because a template name looks through
// the shadow. An enumerator of the exposition-only enumeration is named
// through it, so `fast` is not foreign either.
//
// Each marked class also has a forward declaration ahead of its definition.
// That declaration is reproduced as an exposition-only declaration (issue
// #151): it is usually there because the synopsis needs it to compile, which a
// class marked `\expos` strongly suggests.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_PLAIN_CLASS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_PLAIN_CLASS_HPP

#include "support/spec_expos_plain_targets.hpp"

namespace demo {

namespace detail {
//! \expos
using impl::helper_t;
//! \expos
using impl::count_t;
//! \expos
using impl::mode_t;
} // namespace detail

struct token;

//! \expos
struct token {
    int id;
};

template <class T>
struct box;

//! \expos
template <class T>
struct box {
    T value;
};

class widget {
  public:
    // \ref{widget.ops}, operations
    token           issue(detail::helper_t h) const;
    detail::count_t count() const;
    detail::mode_t  mode() const;
    box<int>        boxed() const;

  private:
    //! \expos
    token last_;
};

// \rSec3[widget.ops]{Operations}

//! \effects-equiv
inline token widget::issue(detail::helper_t h) const { return token{h.value}; }

//! \returns The number of tokens issued.
inline detail::count_t widget::count() const { return 0; }

//! \effects-equiv
inline detail::mode_t widget::mode() const { return detail::mode_t::fast; }

//! \returns A box holding zero.
inline box<int> widget::boxed() const { return box<int>{0}; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_PLAIN_CLASS_HPP
