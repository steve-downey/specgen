// tests/corpus/spec_source_markup_diagnostics.hpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// What spec_source_markup_diagnostics.cc's markup cannot do (issue #149).
// `post` is documented on its declaration as well as its definition in the
// source file, and the declaration's copy is lost, so the Error names the
// definition. The source file's other findings are its own.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_SOURCE_MARKUP_DIAGNOSTICS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_SOURCE_MARKUP_DIAGNOSTICS_HPP

namespace demo {

class ledger {
  public:
    // \ref{ledger.ops}, operations
    //! \effects Posts `amount`.
    void post(int amount);

    void close();
};

void audit(const ledger& book);

// \rSec3[ledger.ops]{Operations}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_SOURCE_MARKUP_DIAGNOSTICS_HPP
