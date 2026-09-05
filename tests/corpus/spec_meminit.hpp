// tests/corpus/spec_meminit.hpp                                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A constructor's mem-initializer list is implementation, never interface
// (issue #21): the splice that removes an in-class body (issue #5) starts at
// the `:` introducing a written ctor-initializer list, so the declaration
// alone renders in the synopsis and the itemdecl. The list used to render
// into both, exposing the private member's spelling (`base_`, never renamed
// to its exposid) and the dropped-qualifier rewrite (`move(base)`). The
// delegating constructor pins the other written-initializer shape. The `std`
// declaration is a local stand-in (the corpus is hermetic).

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_MEMINIT_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_MEMINIT_HPP

namespace std {
template <class T>
constexpr T&& move(T& t) noexcept;
} // namespace std

namespace demo {

// \rSec3[demo.holder]{Class template `holder`}

template <typename R>
class holder {
    //! \expos
    R base_;
    //! \expos
    int count_;

  public:
    // \ref{demo.holder}, construction
    //! \effects Initializes `base_` with `std::move(base)` and `count_` with `1`.
    constexpr explicit holder(R base) : base_(std::move(base)), count_(1) {}

    //! \effects Equivalent to `holder(R())`.
    constexpr holder() : holder(R()) {}
};

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_MEMINIT_HPP
