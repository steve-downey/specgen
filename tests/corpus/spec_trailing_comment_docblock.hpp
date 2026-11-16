// tests/corpus/spec_trailing_comment_docblock.hpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A plain comment after a declaration, on its line, does not cost the
// declaration the docblock above it (issue #296). Clang looks behind a
// declaration before it looks ahead of it, and under -fparse-all-comments an
// ordinary comment that follows code on its line is a trailing comment, so it
// became the declaration's attached comment and the `//!` block was never
// read. Every marker that acts on an in-class declaration was lost that way,
// silently:
//
//  - `\expos` on `size_`: the member was unmarked private data, and dropped;
//  - `\omit` on `impl()`: the member stayed in the synopsis, undescribed;
//  - `\freestanding` on `capacity()`: the suffix was not written;
//  - the description on the hidden friend `swap`, whose markup is in the
//    class: the item lost its *Effects*.
//
// A synopsis keeps a plain comment only when it is marked (`\syn`), so the
// notes themselves are not in the output.
//
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_TRAILING_COMMENT_DOCBLOCK_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_TRAILING_COMMENT_DOCBLOCK_HPP

namespace demo {

class bin {
  public:
    // \ref{demo.bin.obs}, observers
    int find(int key) const; // linear in the number of elements

    //! \freestanding
    int capacity() const; // fixed when the bin is constructed

    //! \omit
    void impl(); // reached only from the constructor

    //! \effects Exchanges the contents of `a` and `b`.
    friend void swap(bin& a, bin& b) { a.size_ = b.size_; } // found by argument-dependent lookup

  private:
    //! \expos
    int size_ = 0; // the number of elements held
};

// \rSec3[demo.bin.obs]{Observers}

//! \returns The position of `key`.
inline int bin::find(int key) const { return key + size_; }

//! \returns The number of elements the bin can hold.
inline int bin::capacity() const { return size_; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_TRAILING_COMMENT_DOCBLOCK_HPP
