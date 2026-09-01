// tests/corpus/spec_diagnostics.hpp                                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (decision hermetic-corpus): build_document()
// has a diagnostic channel, so a
// malformed `\rSec` is reported instead of silently skipped. Deliberately
// synthetic: no other corpus header contains a malformed marker, and none
// should, because every other one backs a byte-exact golden.
//
// The point of the fixture is the *contrast*. Four comment shapes that all
// fail parse_rsec() sit in one file:
//
//   - this license/SPDX block and the prose you are reading (no `\rSec`),
//   - a `\ref{...}` group header inside the class body, which shares
//     `\rSec`'s leading backslash-r and so fails only *after* the parser has
//     consumed input — the case an "did the failure offset move at all"
//     test would misreport,
//   - `\rSec` with an out-of-range depth (overflows std::stoi;
//     reported as a positioned parse failure),
//   - `\rSec` with a well-formed depth and stable name but no `{title}`.
//
// Only the last two are malformed markers, so only they are diagnostics; the
// first two are non-matches and must stay silent. The well-formed `\rSec3`
// sections below keep the document non-trivial, so the test can also pin
// that reporting a diagnostic does not disturb which nodes land in the tree.
//
// **This header's job also covers docblock findings.** The findings the
// docblock grammar computes — the element-ordering Note, the
// duplicate-element Warning, the unknown-tag Error — are printed by
// `generate` on stderr, and stderr is what `golden.diagnostics`
// compares, so a finding's text is reviewable golden text like any
// other output (design §9). Three channels carry docblock findings
// — ItemDecl, SynopsisDecl, and Ignored — and the
// three docblock cases below are chosen to exercise exactly one
// per channel that carries them:
//
//   - `gadget::value()`, an out-of-line definition: `\effects` after
//     `\remarks` (the ItemDecl channel, and an easy mistake for a corpus
//     header to make),
//   - `operator==`, a hidden friend: two `\effects` elements (the
//     SynopsisDecl channel, which carries in-class members' findings),
//   - `gadget::scratch()`, an `\omit`ted definition whose docblock names a
//     tag that does not exist (the Ignored channel: the entity is unspecified
//     on purpose, the typo in the block saying so is not).
//
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_DIAGNOSTICS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_DIAGNOSTICS_HPP

namespace demo {

class gadget {
  public:
    // \ref{gadget.cons}, constructors
    gadget();

    // \ref{gadget.obs}, observers
    int value() const;

    //! \effects Returns `true` if both operands hold the same value.
    //! \effects A second `\effects` element: the grammar keeps both and warns.
    friend bool operator==(const gadget& a, const gadget& b) { return a.value_ == b.value_; }

    void scratch();

  private:
    int value_ = 0;
};

// \rSec3[gadget.cons]{Constructors}

//! \effects Constructs a `gadget` holding no value.
gadget::gadget() : value_(0) {}

// \rSec3[gadget.obs]{Observers}

//! \remarks Written before the `\effects` it belongs after, which is what the
//! ordering lint is for; the rendered output is canonicalized regardless.
//! \effects Returns the held value.
int gadget::value() const { return value_; }

//! \omit
//! \effect A tag that does not exist; the element is `\effects`.
void gadget::scratch() {}

// \rSec99999999999[gadget.overflow]{Depth that does not fit in an int}

// \rSec3[gadget.untitled]

// Prose citing [structure.specifications]
/// END [gadget.syn]
// 22.5.3.4 Modifiers[gadget.mod]

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_DIAGNOSTICS_HPP
