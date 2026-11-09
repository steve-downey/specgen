// tests/split_inline/cases/comments/input.hpp                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The comment table of docs/plans/split-inline.md, one member per row, under
// --move-comments: a docblock across a blank line moves; one above an access
// specifier stays; a trailing comment on one definition's line never opens
// the next member's run; a `/*! */` block moves; Doxygen above a docblock
// stays while the docblock moves; a block of markers only moves too, since
// the driver reads `\omit` and `\merge` from the definition; a plain line
// below the docblock moves with it; a directive between comment and
// declaration keeps them apart.

#ifndef BEMAN_SPECGEN_SPLIT_INLINE_COMMENTS_INPUT_HPP
#define BEMAN_SPECGEN_SPLIT_INLINE_COMMENTS_INPUT_HPP

namespace demo {

struct probe {
    // \ref{probe.ops}, operations

    void f();

    //! \effects Two.
  public:
    void g();

    void h(); //! Three, trailing.
    void i();
    /// Doxygen brief for j.
    void j();
    void k();
    void l();
    //! \effects Seven.
#define BEMAN_SPECGEN_SPLIT_INLINE_NOTHING
    void m();
};

// \rSec3[probe.ops]{Operations}

//! \effects One.
inline void probe::f() {}

inline void probe::g() {}

inline void probe::h() {}

/*! \effects Four. */
inline void probe::i() {}

//! \effects Five.
inline void probe::j() {}

//! \omit
inline void probe::k() {}

//! \effects Six.
// a plain line under the docblock
inline void probe::l() {}

inline void probe::m() {}

} // namespace demo

#endif // BEMAN_SPECGEN_SPLIT_INLINE_COMMENTS_INPUT_HPP
