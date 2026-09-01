// tests/corpus/spec_authored_itemize.hpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Authored itemization and authored prose cross-references. Unlike a
// derived conjunction, each \item keeps the punctuation the author supplied.
// The final item cites an external standard subclause; RefInline deliberately
// does not require its target to be a section in this generated document.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_AUTHORED_ITEMIZE_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_AUTHORED_ITEMIZE_HPP

namespace demo {

class widget {
  public:
    // \ref{widget.mod}, modifiers
    void configure(int mode);
};

// \rSec3[widget.mod]{Modifiers}

//! \constraints All of the following are true:
//! \item `mode >= 0`,
//!
//! \item `mode <= 2`, and
//! \item `mode` denotes a supported mode as specified in \iref{external.mode.requirements}.
//!
//! \effects Configures the widget.
inline void widget::configure(int mode) { (void)mode; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_AUTHORED_ITEMIZE_HPP
