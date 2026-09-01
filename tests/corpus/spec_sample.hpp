// tests/corpus/spec_sample.hpp                                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (decision hermetic-corpus). Small Beman-style
// shape: a declaration-only class with an out-of-line member definition, a
// draft-form group comment inside the class body, a \rSec3 section header in
// the definition region, and a //! docblock on the definition — just enough
// surface to exercise the decl/comment interleave (design §3.2).
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_SAMPLE_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_SAMPLE_HPP

namespace demo {

class sample {
  public:
    // \ref{sample.observers}, observers
    bool observe() const;

  private:
    int value_ = 0;
};

// \rSec3[sample.observers]{Observers}

//! \effects None.
//! \returns `true`.
bool sample::observe() const { return true; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_SAMPLE_HPP
