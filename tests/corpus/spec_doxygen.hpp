// tests/corpus/spec_doxygen.hpp                                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (decision hermetic-corpus): a header
// carrying **Doxygen** comments as well as specgen markup.
//
// It is `spec_widget.hpp` — the same class, the same two `\ref` groups, the
// same two `\rSec3` sections, the same wording, renamed `widget` to `gadget`
// — with a `///` or `/** */` block added at each position a real header puts
// one, and nothing else changed. That is deliberate, and it is the gate:
// `doxygen/expected.json` is `widget_skeleton/expected.json` modulo the
// rename, so the claim under test is not "the output is clean" but "Doxygen
// changed nothing".
//
// The real `beman/optional` header sets the stakes. Were
// `docblock_start` to accept `///` alongside `//!` — a spelling no
// corpus header writes (zero occurrences against 95 `//!` docblocks)
// and that real header writes 31 times — every one of its
// API-documentation blocks would be fed to the docblock grammar, to be
// reported as `prose before first element tag` and built into an
// itemdescr of prose the draft would never print. `/** */` kept as
// draft-form would mean that prose printed into the synopsis.
//
// Each Doxygen block below is one row of the resulting three-way split:
//
//   - Alone in the class body, above `explicit gadget(int)`, above `empty()`,
//     above the private `value_`, and above `class gadget` itself. None of
//     these is markup, so none becomes a descr; none is draft-form either, so
//     none survives into the synopsis.
//   - Directly under the `\ref{gadget.cons}` group header, where Clang merges
//     the `///` lines and the draft-form `//` line above them into a single
//     RawComment. The group header survives and the Doxygen does not, which
//     is the case a scan that looked only at a comment's first line gets
//     backwards.
//   - Directly above a `//!` docblock, in both spellings, at the three
//     out-of-line definitions — the same merge, the other way round. Only the
//     markup half is read.
//
// `gadget()`'s `///` block names `\effect`, a nonexistent tag. specgen
// reports an unknown tag as an Error, so if anything ever parses this block
// again it says so out loud rather than quietly producing wording out of
// implementation notes.
//
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_DOXYGEN_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_DOXYGEN_HPP

namespace demo {

/** A value type, documented for implementers in Doxygen's block form. */
class gadget {
  public:
    // \ref{gadget.cons}, constructors
    /// Constructs an empty gadget.
    /// \effect A tag that does not exist; the element would be `\effects`.
    gadget();
    /** Constructs a gadget holding `value`. */
    explicit gadget(int value);

    // \ref{gadget.observers}, observers
    /** Whether the gadget holds no value. */
    bool empty() const;

  private:
    /** Storage for the held value. */
    int value_ = 0;
};

// \rSec3[gadget.cons]{Constructors}

/// Implementation note: zero-initializes the stored value.
//! \effects Constructs a `gadget` holding no value.
gadget::gadget() : value_(0) {}

/** Implementation note: stores `value` as given. */
//! \effects Constructs a `gadget` holding `value`.
gadget::gadget(int value) : value_(value) {}

// \rSec3[gadget.observers]{Observers}

/** Implementation note: reads the stored value directly. */
//! \effects None.
//! \returns `true` if the gadget holds no value, `false` otherwise.
bool gadget::empty() const { return value_ == 0; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_DOXYGEN_HPP
