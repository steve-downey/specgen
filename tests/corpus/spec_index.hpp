// tests/corpus/spec_index.hpp                                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// AST-derived indexes on wording itemdecls and facility-name spans in
// a synopsis. `value_type` is deliberately synopsis-only; the functions each
// have a wording item and therefore carry standalone index metadata.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_INDEX_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_INDEX_HPP

namespace demo {

class indexed {
  public:
    using value_type = int;

    // \ref{indexed.cons}, constructors
    indexed();

    // \ref{indexed.dtor}, destructor
    ~indexed();

    // \ref{indexed.access}, observers
    int value() const;

    //! \returns `true`.
    friend bool operator==(indexed, indexed) { return true; }
};

// \rSec3[indexed.cons]{Constructors}

//! \effects Constructs an `indexed` object.
indexed::indexed() = default;

// \rSec3[indexed.dtor]{Destructor}

//! \effects Destroys an `indexed` object.
indexed::~indexed() = default;

// \rSec3[indexed.access]{Observers}

//! \returns Zero.
int indexed::value() const { return 0; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_INDEX_HPP
