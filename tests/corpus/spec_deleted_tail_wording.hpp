// tests/corpus/spec_deleted_tail_wording.hpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A deleted function's tail reads `= delete;` in the wording however the
// header spells it (issue #282). spec_deleted_tail_spelling.hpp pins that the
// spelling does not decide where the declaration *ends*; this pins what the
// declaration *says* at the three places one is printed:
//
//  - a class synopsis, where a tail spelled through a macro used to print the
//    macro and its argument, and P2573's `= delete("reason")` its message;
//  - the item declaration of a `\describe`d deleted member, which for the
//    macro spelling used to be a lone `;`, the whole declaration lost;
//  - a gathered header synopsis, where a deleted namespace-scope function
//    used to stop at its parameter list, with no `= delete` and no `;`.
//
// A message is the implementation's diagnostic and a macro is the
// implementation's spelling of a keyword; the draft's synopses carry neither.
// `operator=` and the defaulted template argument are here because the tail
// starts at the *last* `=` outside a bracket, and both write an earlier one.
//
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_DELETED_TAIL_WORDING_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_DELETED_TAIL_WORDING_HPP

#if defined(__cpp_deleted_function)
    #define BEMAN_SPECGEN_CORPUS_DELETED(msg) delete (msg)
#else
    #define BEMAN_SPECGEN_CORPUS_DELETED(msg) delete
#endif

namespace demo {

// \rSec2[demo.latch.syn]{Header synopsis}

class latch;

void rearm(latch&)   = BEMAN_SPECGEN_CORPUS_DELETED("latch: a latch is not rearmed");
void release(latch&) = delete ("latch: a latch is not released");
void reset(latch&)   = delete;

/// END [demo.latch.syn]

// \rSec2[demo.latch]{Class latch}

class latch {
  public:
    // \ref{demo.latch.cons}, constructors
    explicit latch(int count);

    //! \describe
    //! \remarks A latch does not count in floating point.
    latch(double) = BEMAN_SPECGEN_CORPUS_DELETED("latch: no floating point");

    //! \describe
    //! \remarks A latch is not constructed from a pointer.
    latch(const void*) = delete ("latch: no pointers");

    latch(const latch&) = BEMAN_SPECGEN_CORPUS_DELETED("latch: not copyable");

    template <class U = int>
    latch& operator=(U&&) = BEMAN_SPECGEN_CORPUS_DELETED("latch: not assignable");

    friend void swap(latch&, latch&) = delete ("latch: not swappable");

  private:
    int count_ = 0;
};

// \rSec3[demo.latch.cons]{Constructors}

//! \effects Initializes the count with `count`.
inline latch::latch(int count) : count_(count) {}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_DELETED_TAIL_WORDING_HPP
