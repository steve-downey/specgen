// tests/corpus/document/widget.hpp                                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_DOCUMENT_WIDGET_HPP
#define BEMAN_SPECGEN_CORPUS_DOCUMENT_WIDGET_HPP

namespace demo {

class widget {
    //! \expos
    int count_ = 0;

  public:
    // \ref{span.widget}, observers
    int count() const;
};

// The out-of-line definition carries the wording, as the house style has it.
// It is inside the region too, and lists no second `count` in the synopsis:
// the class declared it already.
//! \returns The number of things.
inline int widget::count() const { return count_; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_DOCUMENT_WIDGET_HPP
