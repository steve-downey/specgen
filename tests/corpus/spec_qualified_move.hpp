// tests/corpus/spec_qualified_move.hpp                           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_QUALIFIED_MOVE_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_QUALIFIED_MOVE_HPP

#include "support/spec_foreign_move.hpp"

namespace demo {

// \rSec3[gadget.class]{Class template gadget}

//! These wrappers can store, move, and call callable objects.
template <class T>
class gadget {
  public:
    // Wrappers store, move, and call objects.
    //! \at gadget.class
    //! \effects Equivalent to: `gadget(std::move(g)).swap(*this);`
    void assign(gadget&) {}

  private:
    //! \omit
    void relocate() { o_ = detail::op::move; }

    //! \omit
    detail::op o_ = detail::op::destroy;
};

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_QUALIFIED_MOVE_HPP
