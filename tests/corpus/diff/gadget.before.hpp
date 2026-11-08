// tests/corpus/diff/gadget.before.hpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (decision hermetic-corpus): the *baseline* half
// of the wording diff's fixture pair (docs/plans/wording-diff.md). Its twin,
// gadget.after.hpp, is this header one revision later: `ready()` gains
// `noexcept` and a *Complexity*, its *Returns* is reworded; `reset()` is renamed
// `clear()`, which the diff reads as a removal and an addition; `arm()` keeps
// its signature while its *Preconditions* are reworded and its *Effects* gain
// a second paragraph; `level()` does not change at all; and a new observer
// group and clause, [gadget.query], appear. Self-contained (no #includes) and
// built from bool/int only, so it parses standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_DIFF_GADGET_BEFORE_HPP
#define BEMAN_SPECGEN_CORPUS_DIFF_GADGET_BEFORE_HPP

namespace demo {

class gadget {
  public:
    // \ref{gadget.observe}, observers
    bool ready() const;
    int  level() const;

    // \ref{gadget.mod}, modifiers
    void reset();
    void arm(int level);

  private:
    int level_ = 0;
};

// \rSec3[gadget.observe]{Observers}

//! \returns `true` if the gadget is ready.
bool gadget::ready() const { return level_ > 0; }

//! \returns The current level.
//! \remarks A gadget starts at level zero.
int gadget::level() const { return level_; }

// \rSec3[gadget.mod]{Modifiers}

//! \effects Resets the gadget to level zero.
void gadget::reset() { level_ = 0; }

//! \expects `level` is positive.
//! \effects Arms the gadget at `level`.
void gadget::arm(int level) { level_ = level; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_DIFF_GADGET_BEFORE_HPP
