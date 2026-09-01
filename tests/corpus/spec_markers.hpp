// tests/corpus/spec_markers.hpp                                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (design §4.3: structural markers).
// Exercises `\also` grouping and `\omit` over out-of-line function
// definitions: `front()` is described, `front() const` carries bare `\also` and
// joins its signature onto `front()`'s itemdecl block rather than starting
// its own SpecItem. The four `read()` ref-qualified overloads are interleaved
// left/right and use `\group <id>` plus `\also <id>` to form two non-adjacent
// groups. `scratch()` carries `\omit` and produces no itemdescr at
// all (and its declaration is dropped from the class synopsis, with an
// `omitted` roster entry); `size()` is a plain, undisturbed
// described member rounding out the section. Self-contained (no #includes)
// and built from bool/int only, so it parses standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_MARKERS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_MARKERS_HPP

namespace demo {

class span {
  public:
    // \ref{span.access}, access
    int&       front();
    const int& front() const;
    int        read() &;
    int        read() &&;
    int        read() const&;
    int        read() const&&;
    int&       scratch();
    int        size() const;

  private:
    int data_ = 0;
};

// \rSec3[span.access]{Access}

//! \returns A reference to the first element.
int& span::front() { return data_; }

//! \also
const int& span::front() const { return data_; }

//! \group lvalue
//! \freestanding
//! \returns The lvalue result.
int span::read() & { return 1; }

//! \group rvalue
//! \freestanding-deleted
//! \returns The rvalue result.
int span::read() && { return 2; }

//! \also lvalue
int span::read() const& { return 3; }

//! \also rvalue
int span::read() const&& { return 4; }

//! \omit
int& span::scratch() { return data_; }

//! \effects Returns the number of elements.
int span::size() const { return 0; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_MARKERS_HPP
