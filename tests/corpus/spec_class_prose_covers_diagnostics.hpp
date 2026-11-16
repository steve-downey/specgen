// tests/corpus/spec_class_prose_covers_diagnostics.hpp             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// `\covers mandates P` on a class definition, misplaced (issue #283). The
// claim says the class's leading prose stands for the paragraph its
// static_asserts derive, so each of these is an Error rather than a silent
// drop: a class with no static_assert has no such paragraph, a docblock with
// no leading prose has nothing to stand for it, and a class derives no
// Constraints for `\covers constraints` to be about. On a function the scoped
// form still needs `\asdraft`, as before.
//
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_CLASS_PROSE_COVERS_DIAGNOSTICS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_CLASS_PROSE_COVERS_DIAGNOSTICS_HPP

namespace demo {

template <class T>
inline constexpr bool is_object_like = true;

// \rSec2[demo.misuse]{Misplaced claims}

//! A class with no assertion.
//! \covers mandates is_object_like<E>
template <class E>
class unasserted {};

//! \remarks A class whose docblock has no leading prose.
//! \covers mandates is_object_like<E>
template <class E>
class unworded {
    static_assert(is_object_like<E>, "E must be an object type");
};

//! A class derives no Constraints.
//! \covers constraints is_object_like<E>
template <class E>
class unconstrained {
    static_assert(is_object_like<E>, "E must be an object type");
};

//! \effects Nothing.
//! \covers mandates is_object_like<int>
inline void on_a_function() { static_assert(is_object_like<int>); }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_CLASS_PROSE_COVERS_DIAGNOSTICS_HPP
