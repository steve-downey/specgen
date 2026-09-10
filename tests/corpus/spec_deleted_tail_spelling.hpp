// tests/corpus/spec_deleted_tail_spelling.hpp                      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// How a `= delete`/`= default` tail is *spelled* must not decide whether the
// declaration carrying it is removed whole (issue #99). Two spellings defeat
// reading the tail out of the source text: one written through a macro,
// where raw-lexing forward finds the macro's own identifier rather than a
// keyword, and P2573's `= delete("reason")`, where the keyword is found but
// the parenthesized message is no part of it and is left stranded. Both used
// to leave the same dangling fragment behind that issue #85 fixed for the
// plain keyword -- `= MACRO(...)` or `("reason")` on a line of its own, with
// no declarator, invalid C++ in the rendered synopsis.
//
// The literal `= delete;` / `= default;` spellings are here too, rendered
// rather than merged: they were already correct, and the point of the file
// is that every spelling now agrees.
//
// `operator=` covers the second place the tail's extent is measured:
// `\freestanding-deleted` appends its literal comment suffix after the
// declaration's `;`, which for the P2573 form is past the message and not
// past the keyword.
//
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_DELETED_TAIL_SPELLING_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_DELETED_TAIL_SPELLING_HPP

// bemanproject/expected's own shape, and the motivating case: one spelling
// of the declaration, a P2573 message where the dialect has one and a plain
// `delete` where it does not, so a pre-C++26 build needs no second code
// path. Which branch is live must not change the rendered synopsis, since
// the declaration goes away either way.
#if defined(__cpp_deleted_function)
    #define BEMAN_SPECGEN_CORPUS_DELETE_MSG(msg) delete (msg)
#else
    #define BEMAN_SPECGEN_CORPUS_DELETE_MSG(msg) delete
#endif

#define BEMAN_SPECGEN_CORPUS_DEFAULT default

namespace demo {

// \rSec2[demo.gauge]{Gauge}

//! \remarks A gauge.
template <class T>
class gauge {
  public:
    //! \at demo.gauge
    //! \effects Does the thing.
    constexpr explicit gauge(int g) {}

    //! \merge
    template <class G>
        requires(sizeof(G) > 0)
    constexpr gauge(G) = BEMAN_SPECGEN_CORPUS_DELETE_MSG("gauge: only int is accepted");

    //! \merge
    constexpr gauge(double) = delete ("gauge: only int is accepted");

    //! \merge
    constexpr gauge(const gauge&) = BEMAN_SPECGEN_CORPUS_DEFAULT;

    constexpr gauge()        = default;
    constexpr gauge(gauge&&) = delete;

    //! \freestanding-deleted
    constexpr gauge& operator=(const gauge&) = delete ("gauge: not assignable");

  private:
    T v;
};

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_DELETED_TAIL_SPELLING_HPP
