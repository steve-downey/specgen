// tests/corpus/spec_inclass.hpp                                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (design §3.3 sub-case, §6: in-class-
// defined members and hidden friends). Under one `\rSec3` section, three
// members declared in class-body order exercise the two attachment paths that
// interleave: `front()` is an ordinary out-of-line member (itemdecl from the
// in-class declaration, markup at out-of-line definition — the chain path);
// `size()` is an in-class-defined member (the compiler-bug shape: body and
// `//!` docblock both in the class); `operator==` is a hidden friend (in-class
// by nature, `friend` kept in the itemdecl). All three sit under one
// `\ref{buffer.access}` group, so their itemdescrs must land in the
// `buffer.access` section in class-body order — front(), size(), operator== —
// with the injected in-class items sorted against the out-of-line sibling.
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_INCLASS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_INCLASS_HPP

namespace demo {

class buffer {
  public:
    // \ref{buffer.access}, access
    int& front();

    //! \returns The number of elements.
    int size() const { return count_; }

    //! \returns `true` when the two buffers have equal size.
    friend bool operator==(buffer a, buffer b) { return a.count_ == b.count_; }

  private:
    int count_ = 0;
};

// \rSec3[buffer.access]{Access}

//! \effects Returns a reference to the first element.
int& buffer::front() { return count_; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_INCLASS_HPP
