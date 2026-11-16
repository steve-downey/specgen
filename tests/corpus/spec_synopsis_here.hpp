// tests/corpus/spec_synopsis_here.hpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// `\synopsis-here` says where the class synopsis stands among the paragraphs
// of a class's docblock (issue #292). All of a class's own wording used to
// follow its synopsis, and the draft does not always order a general
// subclause that way: in [expected.un.general] the paragraph that opens the
// subclause, "Subclause [expected.unexpected] describes the class template
// `unexpected` ...", is before the synopsis, and the "is ill-formed"
// paragraph is after it.
//
// A detached block before the class is not a way to write the first of
// those: a lone docblock before a class is the class's own however many
// blank lines separate them, and lands after the synopsis.
//
// `holder` is the draft's shape: one paragraph before the marker, one after,
// the second standing for the paragraph its static_asserts derive
// (`\covers mandates`, issue #283). `plain` puts all its prose before the
// synopsis and leaves its derived paragraph and its *Remarks* after. `late`
// has no marker and is as before. `routed` sends its own wording to another
// section with `\at`, where there is no synopsis to stand ahead of, so the
// opening prose leads the class's nodes there.
//
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_SYNOPSIS_HERE_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_SYNOPSIS_HERE_HPP

namespace demo {

template <class T>
inline constexpr bool is_object_like = true;

// \rSec2[demo.holder]{Class template holder}

//! Subclause \iref{demo.holder} describes the class template `holder`.
//! \synopsis-here
//! A program that instantiates the definition of `holder` for a non-object
//! type is ill-formed.
//! \covers mandates is_object_like<E>
template <class E>
class holder {
    static_assert(is_object_like<E>, "E must be an object type");
};

// \rSec2[demo.plain]{Class template plain}

//! Subclause \iref{demo.plain} describes the class template `plain`.
//!
//! It holds nothing.
//! \synopsis-here
//! \remarks A `plain` is trivially copyable.
template <class E>
class plain {
    static_assert(is_object_like<E>, "E must be an object type");
};

// \rSec2[demo.late]{Class template late}

//! Subclause \iref{demo.late} describes the class template `late`.
template <class E>
class late {};

// \rSec2[demo.routed]{Class template routed}

//! \at demo.routed.general
//! Subclause \iref{demo.routed} describes the class template `routed`.
//! \synopsis-here
//! \remarks A `routed` is described in its general subclause.
template <class E>
class routed {};

// \rSec3[demo.routed.general]{General}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_SYNOPSIS_HERE_HPP
