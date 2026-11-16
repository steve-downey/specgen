// tests/corpus/spec_class_prose_covers.hpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A class's own prose standing for the paragraph its static_asserts derive
// (issue #283). The derivation writes "A program that instantiates `C<T>` is
// ill-formed unless ...", and the draft states the same requirement as an
// unlabelled paragraph in its own words: [expected.un.general] has "A program
// that instantiates the definition of `unexpected` for a non-object type, an
// array type, ... is ill-formed." Writing that sentence as leading prose used
// to state the requirement twice, and an authored `\mandates` replaces the
// derived paragraph but renders under a label the draft does not use.
//
// `\covers mandates P` on the class's docblock says the leading prose is that
// paragraph: the derived one is not rendered, and its conjuncts stay as the
// drift evidence an authored `\mandates` keeps. Each claim answers one
// conjunct the prose covers in words the validator does not read.
//
// `holder` claims both of its conjuncts. `slot` claims one and states the
// other the way the validator reads, so that one needs no claim. `plain`
// writes no claim, and keeps the derived paragraph ahead of its prose as
// before.
//
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_CLASS_PROSE_COVERS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_CLASS_PROSE_COVERS_HPP

namespace demo {

template <class T>
inline constexpr bool is_object_like = true;
template <class T>
inline constexpr bool is_array_like = false;

// \rSec2[demo.holder]{Class template holder}

//! Subclause \iref{demo.holder} describes the class template `holder`.
//!
//! A program that instantiates the definition of `holder` for a non-object
//! type or an array type is ill-formed.
//! \covers mandates is_object_like<E>
//! \covers mandates !is_array_like<E>
template <class E>
class holder {
    static_assert(is_object_like<E>, "E must be an object type");
    static_assert(!is_array_like<E>, "E must not be an array type");
};

// \rSec2[demo.slot]{Class template slot}

//! A program that instantiates the definition of `slot` for a non-object type
//! is ill-formed, and `is_array_like<E>` is `false`.
//! \covers mandates is_object_like<E>
template <class E>
class slot {
    static_assert(is_object_like<E>, "E must be an object type");
    static_assert(!is_array_like<E>, "E must not be an array type");
};

// \rSec2[demo.plain]{Class template plain}

//! Subclause \iref{demo.plain} describes the class template `plain`.
template <class E>
class plain {
    static_assert(is_object_like<E>, "E must be an object type");
};

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_CLASS_PROSE_COVERS_HPP
