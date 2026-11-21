// tests/corpus/spec_forward_move.hpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_FORWARD_MOVE_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_FORWARD_MOVE_HPP

// Hand-curated corpus header (decision hermetic-corpus): the std names the
// bodies call are declared here rather than included, so the parse reaches
// nothing outside the corpus. The signatures are simplified: a class template
// here, such as remove_reference, would be harvested as a synopsis.
namespace std {
template <class T>
constexpr T&& forward(T& t) noexcept;
template <class T>
constexpr T&& move(T& t) noexcept;
template <class F, class... Args>
constexpr decltype(auto) invoke(F&& f, Args&&... args);
template <class T, class U = T>
constexpr T exchange(T& obj, U&& new_value);
} // namespace std

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
