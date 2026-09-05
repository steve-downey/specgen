// tests/corpus/spec_gathered_refs.hpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// `\ref` group headers inside a gathered header synopsis (issue #31): a
// header written *inside a class body* is already carried by the class's
// extracted synopsis, so the gathered fold must not append it a second time
// as a standalone group header — while a header written at namespace scope,
// between declarations, is exactly what the standalone append exists for and
// still survives. One region holds both.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_GATHERED_REFS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_GATHERED_REFS_HPP

// \rSec2[demo.syn]{Header synopsis}

namespace demo {

template <typename I>
class holder {
    //! \expos
    I ptr_;

  public:
    // \ref{demo.holder}, construction
    //! \effects Initializes the holder with `ptr`.
    constexpr explicit holder(I ptr);
};

// \ref{demo.ops}, operations
template <typename I>
void swap(holder<I>&, holder<I>&);

} // namespace demo

/// END [demo.syn]

// \rSec3[demo.holder]{Class template `holder`}

namespace demo {

//! \effects Initializes the holder with `ptr`.
template <typename I>
constexpr holder<I>::holder(I ptr) : ptr_(ptr) {}

} // namespace demo

// \rSec3[demo.ops]{Operations}

namespace demo {

//! \effects Exchanges the states of `a` and `b`.
template <typename I>
void swap(holder<I>& a, holder<I>& b) {}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_GATHERED_REFS_HPP
