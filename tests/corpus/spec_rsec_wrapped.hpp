// tests/corpus/spec_rsec_wrapped.hpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A `\rSec` marker whose title clang-format wrapped across two plain `//`
// lines (issue #8): the marker parses whole, the section opens with the
// joined title, and the member routed to it validates — the wrap used to
// read as a malformed marker, the section silently did not exist, and every
// routed member failed validation instead. The title is deliberately long
// enough that the formatter keeps the wrap.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_RSEC_WRAPPED_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_RSEC_WRAPPED_HPP

namespace demo {

// \rSec2[demo.accumulating]{Accumulating applicative instance for expected with error accumulation across the whole
// applicative chain}

//! \returns The accumulated value unchanged.
inline int accumulate_probe(int value) { return value; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_RSEC_WRAPPED_HPP
