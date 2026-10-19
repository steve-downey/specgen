// tests/corpus/spec_requires_terminator.hpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A requires-clause whose last line ends in a nested template-argument list
// (issue #157). When that line lands exactly on the column limit, clang-format
// ends it there and puts the declaration's `;` on a line of its own; the front
// end reformats a column narrower until the `;` ends the clause again. `h` is
// sized so that its itemdecl, which keeps the clause through
// `\constraints-in-decl` and is formatted at the draft's 88 columns, lands
// exactly on the limit.
//
// The class synopsis is the second half: it is formatted, then indented two
// columns inside `namespace std {`, so it formats two columns narrower or its
// longest lines run past the limit.
//
// The `std` stand-ins keep the header hermetic; only their names matter.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_REQUIRES_TERMINATOR_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_REQUIRES_TERMINATOR_HPP

namespace std {
template <class T, class U>
concept same_as = true;
template <class T, class U>
concept convertible_to = true;
template <class T>
using remove_cvref_t = T;
template <class F, class A>
using invoke_result_t = A;
template <class A, class B>
using common_type_t = A;
} // namespace std

namespace demo {

struct widget_t {
    // \ref{widget.ops}, operations
    template <class APPLICATIVE, class FUNCTION, class VALUES>
    void h(APPLICATIVE a, FUNCTION f, VALUES v)
        requires std::same_as<std::remove_cvref_t<VALUES>, int> &&
                 std::convertible_to<
                     APPLICATIVE,
                     std::remove_cvref_t<std::invoke_result_t<FUNCTION&, std::common_type_t<widget_t, VALUES>>>>;
};

// \rSec3[widget.ops]{Operations}

//! \constraints-in-decl
//! \effects Applies `f` to `v` in the context of `a`.
template <class APPLICATIVE, class FUNCTION, class VALUES>
void widget_t::h(APPLICATIVE a, FUNCTION f, VALUES v)
    requires std::same_as<std::remove_cvref_t<VALUES>, int> &&
             std::convertible_to<
                 APPLICATIVE,
                 std::remove_cvref_t<std::invoke_result_t<FUNCTION&, std::common_type_t<widget_t, VALUES>>>>
{}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_REQUIRES_TERMINATOR_HPP
