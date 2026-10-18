// tests/corpus/spec_returns_expr.hpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// `\returns-expr` (design §4.2, issue #143): a definition body that is one
// `return E;` renders as *Returns*: `E`, a bare expression rather than an
// Equivalent to: block, since a Returns names the value and no effects.
//
// - `size` is the plain shape.
// - `front` leads with a static_assert. The Mandates derivation consumes it
//   (design §5.2), so it is not part of the body `\returns-expr` reads, and the
//   item carries both elements.
// - `distance` qualifies a call into `std`; the rendered expression drops the
//   qualifier like every other piece of extracted code.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_RETURNS_EXPR_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_RETURNS_EXPR_HPP

namespace std {
template <class T>
constexpr T abs_diff(T a, T b) {
    return a < b ? b - a : a - b;
}
} // namespace std

namespace demo {

template <class T>
class span {
  public:
    // \ref{span.obs}, observers
    constexpr int size() const;

    constexpr const T& front() const;

    constexpr int distance(int from) const;

  private:
    //! \expos
    const T* data_ = nullptr;
    //! \expos
    int size_ = 0;
};

// \rSec3[span.obs]{Observers}

//! \returns-expr
template <class T>
constexpr int span<T>::size() const {
    return size_;
}

//! \returns-expr
template <class T>
constexpr const T& span<T>::front() const {
    static_assert(sizeof(T) > 0);
    return *data_;
}

//! \returns-expr
template <class T>
constexpr int span<T>::distance(int from) const {
    return std::abs_diff(from, size_);
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_RETURNS_EXPR_HPP
