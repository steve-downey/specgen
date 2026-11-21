// tests/corpus/spec_forward_move.hpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_FORWARD_MOVE_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_FORWARD_MOVE_HPP

#include <functional>
#include <utility>

namespace demo {

// \rSec3[box.class]{Class template box}

//! The draft writes `std::forward` and `std::move` qualified in an extracted
//! body, and every other name in namespace std unqualified (issue #405).
template <class T>
class box {
  public:
    // \ref{box.ops}, operations
    template <class F>
    constexpr auto apply(F&& f) const;
    constexpr T    take();

  private:
    //! \expos(val)
    T value_;
};

// \rSec3[box.ops]{Operations}

//! \effects-equiv
template <class T>
template <class F>
constexpr auto box<T>::apply(F&& f) const {
    return std::invoke(std::forward<F>(f), value_);
}

//! \effects-equiv
template <class T>
constexpr T box<T>::take() {
    return std::exchange(value_, std::move(value_));
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_FORWARD_MOVE_HPP
