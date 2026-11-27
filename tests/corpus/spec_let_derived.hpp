// tests/corpus/spec_let_derived.hpp                            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_LET_DERIVED_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_LET_DERIVED_HPP

// Hand-curated corpus header (decision hermetic-corpus): the std names the
// bodies use are declared here rather than included, so the parse reaches
// nothing outside the corpus.
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
using remove_cvref_t = T;
template <class T>
constexpr bool is_object_v = true;
template <class T>
constexpr bool is_const_v = false;
} // namespace std

namespace demo {

// \rSec3[box.class]{Class template box}

//! The leading aliases that a static_assert names are introduced by one derived
//! "Let" paragraph ahead of the Mandates, and leave the Effects code (issue
//! #404, decision let-binding-wording).
template <class T>
class box {
  public:
    // \ref{box.ops}, operations
    template <class F>
    constexpr auto apply(F&& f) const;
    template <class F>
    constexpr auto chain(F&& f) const;
    template <class F>
    constexpr auto local(F&& f) const;
    constexpr auto member() const;
    template <class F>
    constexpr auto prose(F&& f) const;
    template <class F>
    constexpr auto triple(F&& f) const;
    template <class F>
    constexpr auto joined(F&& f) const;

  private:
    //! \expos(val)
    T* value_ = nullptr;
};

// \rSec3[box.ops]{Operations}

//! \effects-equiv
template <class T>
template <class F>
constexpr auto box<T>::apply(F&& f) const {
    using U = std::invoke_result_t<F, T&>;
    static_assert(std::is_object_v<U>);
    return std::invoke(std::forward<F>(f), *value_);
}

//! An alias the asserted one names is introduced too, in source order.
//!
//! \effects-equiv
template <class T>
template <class F>
constexpr auto box<T>::chain(F&& f) const {
    using U = std::invoke_result_t<F, T&>;
    using V = std::remove_cvref_t<U>;
    static_assert(std::is_object_v<V>);
    return std::invoke(std::forward<F>(f), *value_);
}

//! An alias only the body uses stays in the code.
//!
//! \effects-equiv
template <class T>
template <class F>
constexpr auto box<T>::local(F&& f) const {
    using W = std::invoke_result_t<F, T&>;
    static_assert(std::is_object_v<T>);
    return W(std::invoke(std::forward<F>(f), *value_));
}

//! \effects-equiv
template <class T>
constexpr auto box<T>::member() const {
    using U = decltype((value_));
    static_assert(!std::is_const_v<U>);
    return *value_;
}

//! `U` names the result of calling `f`.
//! \effects-equiv
template <class T>
template <class F>
constexpr auto box<T>::prose(F&& f) const {
    using U = std::invoke_result_t<F, T&>;
    static_assert(std::is_object_v<U>);
    return std::invoke(std::forward<F>(f), *value_);
}

//! Three aliases are bound in one sentence, the last after ", and".
//!
//! \effects-equiv
template <class T>
template <class F>
constexpr auto box<T>::triple(F&& f) const {
    using U = std::invoke_result_t<F, T&>;
    using V = std::remove_cvref_t<U>;
    using W = std::remove_cvref_t<V>;
    static_assert(std::is_object_v<W>);
    return std::invoke(std::forward<F>(f), *value_);
}

//! Let `U` be `invoke_result_t<F, T&>` and `V` be `remove_cvref_t<U>`.
//! \effects-equiv
template <class T>
template <class F>
constexpr auto box<T>::joined(F&& f) const {
    using U = std::invoke_result_t<F, T&>;
    using V = std::remove_cvref_t<U>;
    static_assert(std::is_object_v<V>);
    return std::invoke(std::forward<F>(f), *value_);
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_LET_DERIVED_HPP
