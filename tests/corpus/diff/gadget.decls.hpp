// tests/corpus/diff/gadget.decls.hpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (decision hermetic-corpus): the *current* half of
// the wording diff's fixture pair (docs/plans/wording-diff.md); see
// gadget.before.hpp for what changed between the two. Self-contained (no
// #includes) and built from bool/int only, so it parses standalone under
// -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_DIFF_GADGET_AFTER_HPP
#define BEMAN_SPECGEN_CORPUS_DIFF_GADGET_AFTER_HPP

namespace demo {

class gadget {
  public:
    // \ref{gadget.observe}, observers
    bool              ready() const noexcept;
    [[nodiscard]] int level() const;

    // \ref{gadget.mod}, modifiers
    void clear();
    void arm(int level);

    // \ref{gadget.query}, queries
    bool armed() const;

  private:
    int level_ = 0;
};

// \rSec3[gadget.observe]{Observers}

//! \returns `true` if the gadget is armed.
//! \complexity Constant.
bool gadget::ready() const noexcept { return level_ > 0; }

//! \returns The current level.
//! \remarks A gadget starts at level zero.
int gadget::level() const { return level_; }

// \rSec3[gadget.mod]{Modifiers}

//! \effects Clears the gadget, returning it to level zero.
void gadget::clear() { level_ = 0; }

//! \expects `level` is greater than zero.
//! \effects Arms the gadget at `level`.
//!
//! Arming an armed gadget replaces its level.
void gadget::arm(int level) { level_ = level; }

// \rSec3[gadget.query]{Queries}

//! \returns `true` if the gadget has been armed.
bool gadget::armed() const { return level_ > 0; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_DIFF_GADGET_AFTER_HPP
