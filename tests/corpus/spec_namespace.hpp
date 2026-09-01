// tests/corpus/spec_namespace.hpp                                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (design §3.5: reference-resolved token
// rewriting). The draft writes library names unqualified — the wording lives
// inside namespace std — so a qualifier that *resolves* to `std`, or to the
// header's own namespace (`demo`, which maps onto `std` in the draft), is
// dropped from the synopsis and the itemdecl. A `detail::` qualifier resolves
// to neither and stays verbatim; flagging it is the leakage checker's job
// (design §9) — so this header is the one corpus header that does not
// validate clean, on purpose, and `namespace_map_validate` pins the finding it
// draws instead of `golden.<case>.validate` demanding silence.
//
// `store_` and `spare_` are the same type written two ways — fully
// qualified and abbreviated. Both render `detail::storage`, and one
// finding covers them, because the drop set is prefix-minimal: were it
// to hold the fully-qualified `demo::detail` with the chain compared as
// *written*, `demo::detail::storage` would match, lose its whole
// qualifier, and render as bare `storage` — the leak erased, unreported.
//
// The `std` declarations below are local stand-ins, not the real headers: the
// corpus is hermetic (no #includes), and only the *resolution* matters to
// the rewriter. They are aliases and variable templates rather than classes so
// they contribute no synopsis of their own. Self-contained under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_NAMESPACE_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_NAMESPACE_HPP

namespace std {
using size_type = unsigned long;
template <class T>
constexpr bool is_copy_constructible_v = true;
} // namespace std

namespace demo {

namespace detail {
using storage = int;
} // namespace detail

using count_type = int;

template <class T>
class holder {
  public:
    // \ref{holder.cons}, constructors
    holder(std::size_type n, demo::count_type c);

    // \ref{holder.obs}, observers
    std::size_type size() const;
    bool           copyable() const;

  private:
    //! \expos
    detail::storage store_;
    //! \expos
    demo::detail::storage spare_;
};

// \rSec3[holder.cons]{Constructors}

//! \effects Constructs a `holder` with room for `n` elements and a reference
//! count of `c`.
template <class T>
holder<T>::holder(std::size_type n, demo::count_type c) : store_(static_cast<detail::storage>(n + c)) {}

// \rSec3[holder.obs]{Observers}

//! \returns The number of elements.
template <class T>
std::size_type holder<T>::size() const {
    return 0;
}

//! \returns `true` if the element type is copy constructible.
template <class T>
bool holder<T>::copyable() const {
    return std::is_copy_constructible_v<T>;
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_NAMESPACE_HPP
