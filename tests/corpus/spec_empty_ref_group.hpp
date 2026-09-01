// tests/corpus/spec_empty_ref_group.hpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_EMPTY_REF_GROUP_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_EMPTY_REF_GROUP_HPP

namespace demo {

class grouping {
  public:
    // \ref{grouping.empty}, removed members
    //! \merge
    void erased_one();
    //! \merge
    void erased_two();

    // \ref{grouping.live}, visible members
    void kept() = delete;

    class nested {
      public:
        // \ref{grouping.nested}, nested members
        void member() = delete;
    };
};

// \rSec3[grouping.live]{Visible members}

// \rSec3[grouping.nested]{Nested members}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_EMPTY_REF_GROUP_HPP
