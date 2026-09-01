// tests/corpus/include_path/consumer/spec_include.hpp              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header for the `-I`
// golden: an *angle* include of a header in a sibling directory
// (tests/corpus/include_path/specgen_acid/widget_traits.hpp), so parsing
// this header genuinely depends on `-I tests/corpus/include_path` on the
// command line. It lives under its own `consumer/` directory rather than
// directly beside `specgen_acid/` -- see widget_traits.hpp's own comment for
// why that placement, not just the angle brackets, is what makes the
// dependency real: a quoted-relative include, or an angle one reachable via
// the includer's own directory, would resolve on its own and prove nothing.
// Without the `-I`, the missing include is a `fatal error` (verified against
// clang++ 22 directly) that halts preprocessing of the rest of this file,
// so the whole `generate` fails rather than quietly degrading.
//
// Otherwise a minimal Beman-style shape, the same one spec_widget.hpp uses:
// one class, one in-class-declared observer, one out-of-line definition.

#ifndef BEMAN_SPECGEN_CORPUS_INCLUDE_PATH_CONSUMER_SPEC_INCLUDE_HPP
#define BEMAN_SPECGEN_CORPUS_INCLUDE_PATH_CONSUMER_SPEC_INCLUDE_HPP

#include <specgen_acid/widget_traits.hpp>

namespace demo {

class gadget {
  public:
    // \ref{gadget.observers}, observers
    widget_traits::value_type value() const;
};

// \rSec3[gadget.observers]{Observers}

//! \effects None.
//! \returns `0`.
widget_traits::value_type demo::gadget::value() const { return 0; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_INCLUDE_PATH_CONSUMER_SPEC_INCLUDE_HPP
