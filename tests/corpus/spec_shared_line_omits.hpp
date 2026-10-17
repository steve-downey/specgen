// tests/corpus/spec_shared_line_omits.hpp                           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (design §3.4: omitted private members). Each
// private member below draws two whole-line removals that begin at the same
// line start: an unnamed struct and the field it declares, or the two
// declarators of `int a_, b_;`. The shorter removal ends before the field
// name, so applying it first left `empty_;`, `s_;` or `, b_;` behind in the
// synopsis (issue #147). Edits sharing a begin now apply longest first, so
// every one of them is dropped and only the `\expos` members survive.
// Self-contained (no #includes), so it parses standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_SHARED_LINE_OMITS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_SHARED_LINE_OMITS_HPP

namespace demo {

class widget {
  public:
    // \ref{widget.ops}, operations
    void reset();

  private:
    //! \expos
    bool flag_;
    union {
        struct {
        } empty_;
        //! \expos
        int value_;
    };
    struct {
    } s_;
    int a_, b_;
};

// \rSec3[widget.ops]{Operations}

//! \effects Restores the default state.
inline void widget::reset() {}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_SHARED_LINE_OMITS_HPP
