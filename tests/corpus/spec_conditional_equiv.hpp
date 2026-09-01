// tests/corpus/spec_conditional_equiv.hpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_CONDITIONAL_EQUIV_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_CONDITIONAL_EQUIV_HPP

#define SPECGEN_ACTIVE_BRANCH 1

namespace demo {

class choice {
  public:
    // \ref{choice.ops}, operations
    int  select() const;
    void assign();
};

// \rSec3[choice.ops]{Operations}

//! \returns-equiv
inline int choice::select() const {
#if SPECGEN_ACTIVE_BRANCH
    return 1; // selected return
#else
    return 99;
#endif
}

//! \effects-equiv
inline void choice::assign() {
    int value = 0;
#if 0
    value = 99;
#elif SPECGEN_ACTIVE_BRANCH
    #if 0
    value = 88;
    #else
    value = 1; // selected assignment
    #endif
#else
    value = 2;
#endif
    (void)value;
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_CONDITIONAL_EQUIV_HPP
