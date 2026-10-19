// tests/corpus/spec_declaration_docblocks.hpp                      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Class-body docblocks that can produce no wording of their own, which used to
// be dropped without a word (issues #125, #140). Each wording-bearing one is
// an Error; each marker-only one stays silent, since a marker on a declaration
// is what that docblock is for.
//
// - `insert` is declared with an Effects and defined out of line with its own
//   markup: the declaration's copy is lost, so the Error names the definition.
// - `erase` is declared with a Returns and defined nowhere in this header, as
//   a member defined in a source file is: there is nothing to attach it to.
// - `stride` is a non-static data member and `capacity_` an exposition-only
//   static one, whose Remarks have no itemdescr to become. A documented static
//   data member that is not `\expos` is an item of its own (issue #148).
// - `reserve` and `limit_` carry markers only, and stay silent.
// - `clear` misspells its element, so the docblock asks for nothing, but its
//   grammar finding is still reported: nothing else reads this docblock.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_DECLARATION_DOCBLOCKS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_DECLARATION_DOCBLOCKS_HPP

namespace demo {

class table {
  public:
    // \ref{table.modifiers}, modifiers
    //! \effects Inserts `value`.
    void insert(int value);

    //! \returns The number of elements removed.
    int erase(int value);

    //! \seebelow
    void reserve(int n);

    //! \effect Removes every element.
    void clear();

    //! \remarks The distance between rows.
    int stride = 0;

  private:
    //! \expos
    int limit_ = 0;

    //! \expos
    //! \remarks The most rows a table holds.
    static constexpr int capacity_ = 8;
};

// \rSec3[table.modifiers]{Modifiers}

//! \effects Inserts `value` unless it is already present.
inline void table::insert(int value) { limit_ = value; }

//! \effects Reserves room for `n` elements.
inline void table::reserve(int n) { limit_ = n; }

//! \effects Removes every element.
inline void table::clear() { limit_ = 0; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_DECLARATION_DOCBLOCKS_HPP
