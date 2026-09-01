// tests/corpus/spec_inclass_markers.hpp                            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (design §4.3: the structural markers
// that act on in-class members). Exercises three markers on one class:
//
//   * `\describe` gives an itemdecl to a `= default` member (`gadget(const
//     gadget&)`) that carries description content — an authored `\mandates`
//     and `\effects`, the pattern of tuple's defaulted copy constructor — which
//     would otherwise be a synopsis-only declaration; `= default` is kept;
//   * `\merge` collapses a defaulted twin (`gadget(gadget&&)`) into the copy
//     constructor's entity — dropped from the synopsis and given no itemdecl;
//   * `\at` routes an in-class member's itemdescr to an explicit section:
//     `reset()` sits under the `\ref{gadget.obs}` group but `\at gadget.cons`
//     places its wording in the constructors subclause instead.
//
// Under `gadget.cons` the itemdescrs then order by class-body position:
// gadget() (out-of-line), the described copy constructor (in-class), reset()
// (in-class, routed here by `\at`). Self-contained (no #includes) and built
// from bool/int only, so it parses standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_INCLASS_MARKERS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_INCLASS_MARKERS_HPP

namespace demo {

class gadget {
  public:
    // \ref{gadget.cons}, constructors
    gadget();

    //! \describe
    //! \mandates `is_copy_constructible_v<int>` is `true`.
    //! \effects Initializes each member of `*this` with the corresponding member of the argument.
    gadget(const gadget&) = default;

    //! \merge
    gadget(gadget&&) = default;

    // \ref{gadget.obs}, observers
    bool ready() const;

    //! \at gadget.cons
    //! \effects Resets to the default state.
    void reset() { state_ = 0; }

  private:
    int state_ = 0;
};

// \rSec3[gadget.cons]{Constructors}

//! \effects Constructs an empty gadget.
gadget::gadget() { state_ = 0; }

// \rSec3[gadget.obs]{Observers}

//! \returns `true` if the gadget is ready.
bool gadget::ready() const { return state_ != 0; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_INCLASS_MARKERS_HPP
