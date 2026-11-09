// tests/split_inline/cases/refusals/input.hpp                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// What split_inline refuses (docs/plans/split-inline.md, "Refusals"), one
// member each: a body spelled through a macro, a member inside an `#if` the
// class is not inside, and a member of an unnamed struct. A local class is
// never reached, since the tool reads namespace-scope classes only, so its
// member stays silently. `plain()` is there to show the rest of the header
// still converts.

#ifndef BEMAN_SPECGEN_SPLIT_INLINE_REFUSALS_INPUT_HPP
#define BEMAN_SPECGEN_SPLIT_INLINE_REFUSALS_INPUT_HPP

#define BEMAN_SPECGEN_SPLIT_INLINE_ONE \
    {                                  \
        return 1;                      \
    }
#define BEMAN_SPECGEN_SPLIT_INLINE_GUARDED

namespace demo {

class refusing {
  public:
    int plain() const { return 0; }
    int from_macro() const BEMAN_SPECGEN_SPLIT_INLINE_ONE
#ifdef BEMAN_SPECGEN_SPLIT_INLINE_GUARDED
        int guarded() const {
        return 2;
    }
#endif
    struct {
        int unnamed() const { return 3; }
    } anon;
};

inline int local() {
    struct local_class {
        int member() const { return 4; }
    };
    return local_class{}.member();
}

} // namespace demo

#endif // BEMAN_SPECGEN_SPLIT_INLINE_REFUSALS_INPUT_HPP
