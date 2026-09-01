// tests/corpus/spec_widget.hpp                                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (decision hermetic-corpus). A Beman-style shape
// here: a declaration-only class with two `\ref` group comments in its body
// (constructors, observers), two constructors and one observer declared
// in-class, and a definition region holding two sibling `\rSec3` sections —
// one per group — each with its out-of-line member definitions and a small
// `//!` docblock. Just enough surface to exercise build_document() folding
// `\rSec` comments into nested ir::Section siblings with decls hanging under
// them (design §3.2), without any nesting depth to worry about.
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_WIDGET_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_WIDGET_HPP

namespace demo {

class widget {
  public:
    // \ref{widget.cons}, constructors
    widget();
    explicit widget(int value);

    // \ref{widget.observers}, observers
    bool empty() const;

  private:
    int value_ = 0;
};

// \rSec3[widget.cons]{Constructors}

//! \effects Constructs a `widget` holding no value.
widget::widget() : value_(0) {}

//! \effects Constructs a `widget` holding `value`.
widget::widget(int value) : value_(value) {}

// \rSec3[widget.observers]{Observers}

//! \effects None.
//! \returns `true` if the widget holds no value, `false` otherwise.
bool widget::empty() const { return value_ == 0; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_WIDGET_HPP
