// tests/corpus/spec_sentinel_recovery.hpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Regression fixture for the acid run's sentinel-prefix collision.
// Eleven exposed declarations force recovery to distinguish span 1 from
// spans 10 and above. The terminal declaration detects duplicated suffixes.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_SENTINEL_RECOVERY_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_SENTINEL_RECOVERY_HPP

namespace demo {

class sentinel_case {
    //! \expos
    int field0;
    //! \expos
    int field1;
    //! \expos
    int field2;
    //! \expos
    int field3;
    //! \expos
    int field4;
    //! \expos
    int field5;
    //! \expos
    int field6;
    //! \expos
    int field7;
    //! \expos
    int field8;
    //! \expos
    int field9;
    //! \expos
    int field10;

    // terminal_suffix
};

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_SENTINEL_RECOVERY_HPP
