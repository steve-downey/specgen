// tests/split_inline/cases/comments/input.hpp                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The comment table of docs/plans/split-inline.md, one member per row, under
// --move-comments: a docblock across a blank line moves; one above an access
// specifier stays; a trailing comment on one definition's line never opens
// the next member's run; a `/*! */` block moves; Doxygen above a docblock
// stays while the docblock moves; a markers-only block stays; a plain line
// below the docblock moves with it; a directive between comment and
// declaration keeps them apart.

#ifndef BEMAN_SPECGEN_SPLIT_INLINE_COMMENTS_INPUT_HPP
#define BEMAN_SPECGEN_SPLIT_INLINE_COMMENTS_INPUT_HPP

namespace demo {

struct probe {
    // \ref{probe.ops}, operations
    //! \effects One.

    void f() {}

    //! \effects Two.
  public:
    void g() {}

    void h() {} //! Three, trailing.
    /*! \effects Four. */
    void i() {}
    /// Doxygen brief for j.
    //! \effects Five.
    void j() {}
    //! \omit
    void k() {}
    //! \effects Six.
    // a plain line under the docblock
    void l() {}
    //! \effects Seven.
#define BEMAN_SPECGEN_SPLIT_INLINE_NOTHING
    void m() {}
};

} // namespace demo

#endif // BEMAN_SPECGEN_SPLIT_INLINE_COMMENTS_INPUT_HPP
