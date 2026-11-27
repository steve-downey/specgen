// tests/corpus/spec_let_alias_drift.hpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_LET_ALIAS_DRIFT_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_LET_ALIAS_DRIFT_HPP

// Hand-curated corpus header (decision hermetic-corpus): the std names the
// bodies use are declared here rather than included.
namespace std {
template <class T>
constexpr T&& forward(T& t) noexcept;
template <class T>
T&& declval() noexcept;
template <class F, class... Args>
constexpr decltype(auto) invoke(F&& f, Args&&... args);
template <class F, class... Args>
using invoke_result_t = decltype(invoke(declval<F>(), declval<Args>()...));
template <class T>
constexpr bool is_object_v = true;
} // namespace std

namespace demo {

// \rSec3[box.class]{Class template box}

//! An authored "Let" that does not state an alias exactly leaves the alias in
//! the code, which the validator reports as drift (issue #404, decision
//! let-binding-wording).
template <class T>
class box {
  public:
    // \ref{box.ops}, operations
    template <class F>
    constexpr auto loose(F&& f) const;
    template <class F>
    constexpr auto other(F&& f) const;

  private:
    //! \expos(val)
    T* value_ = nullptr;
};

// \rSec3[box.ops]{Operations}

//! Let `U` be the type `invoke_result_t<F, T&>`.
//! \effects-equiv
template <class T>
template <class F>
constexpr auto box<T>::loose(F&& f) const {
    using U = std::invoke_result_t<F, T&>;
    static_assert(std::is_object_v<U>);
    return std::invoke(std::forward<F>(f), *value_);
}

//! Let `U` be `invoke_result_t<F, const T&>`.
//! \effects-equiv
template <class T>
template <class F>
constexpr auto box<T>::other(F&& f) const {
    using U = std::invoke_result_t<F, T&>;
    static_assert(std::is_object_v<U>);
    return std::invoke(std::forward<F>(f), *value_);
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_LET_ALIAS_DRIFT_HPP
