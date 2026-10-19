// tests/corpus/spec_expos_static_members.hpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// An `\expos` static data member, or static data member template, renders
// like exposition-only data: its exposid name and `// exposition only` tail in
// the synopsis, and its uses in an extracted body rewritten to that name. It
// used to be left out of the synopsis altogether (issue #172). The use of
// `spare_<int>` also instantiates it, which must not count as a member of its
// own.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_STATIC_MEMBERS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_STATIC_MEMBERS_HPP

namespace demo {

class table {
  public:
    // \ref{table.ops}, operations
    //! \effects-equiv
    constexpr void reset() { limit_ = capacity_ + spare_<int>; }

  private:
    //! \expos
    int limit_ = 0;

    //! \expos
    static constexpr int capacity_ = 8;

    //! \expos
    template <class T>
    static constexpr T spare_ = T(2);
};

// \rSec3[table.ops]{Operations}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_STATIC_MEMBERS_HPP
