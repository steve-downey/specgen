// tests/corpus/spec_leading_leak.hpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_LEADING_LEAK_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_LEADING_LEAK_HPP

namespace demo {

class probe {
    int implementation_ = 1;

  public:
    // \ref{leading.leak}, members
    //! Uses `implementation_` without specifying it.
    void inspect() {}
};

// \rSec3[leading.leak]{Leaking prose}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_LEADING_LEAK_HPP
