// tests/corpus/spec_expos_static_missing.hpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_STATIC_MISSING_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_STATIC_MISSING_HPP

class unrouted {
    //! \expos
    //! \at nowhere
    //! \remarks A description must have a destination even for exposition.
    static constexpr int missing_ = 1;

    //! \expos
    //! \remarks Non-static data still has no independent description.
    int field_ = 0;
};

#endif // BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_STATIC_MISSING_HPP
