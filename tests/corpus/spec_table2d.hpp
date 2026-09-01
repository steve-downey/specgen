// tests/corpus/spec_table2d.hpp                                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// An optional.assign-shaped authored two-dimensional effects table.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_TABLE2D_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_TABLE2D_HPP

namespace demo {

class optional {
  public:
    // \ref{optional.assign}, assignment
    optional& operator=(const optional& rhs);

  private:
    //! \expos(val)
    int value_ = 0;
};

// \rSec3[optional.assign]{Assignment}

//! \effects See the following table.
//! \lib2dtab2[optional.assign.copy]{`optional::operator=(const optional&)` effects}
//! \column `*this` contains a value
//! \column `*this` does not contain a value
//! \row `rhs` contains a value
//! \cell assigns the value of `rhs` to `value_`.
//! \cell direct-non-list-initializes `value_` from `rhs`.
//! \row `rhs` does not contain a value
//! \cell destroys `value_`.
//! \cell no effect.
//! \endlib2dtab2
inline optional& optional::operator=(const optional& rhs) {
    (void)rhs;
    return *this;
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_TABLE2D_HPP
