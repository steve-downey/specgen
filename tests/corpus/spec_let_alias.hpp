// tests/corpus/spec_let_alias.hpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_LET_ALIAS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_LET_ALIAS_HPP

// Hand-curated corpus header (decision hermetic-corpus): the std names the
// body uses are declared here rather than included, so the parse reaches
// nothing outside the corpus.
namespace std {
template <class T>
constexpr T&& forward(T& t) noexcept;
template <class F, class... Args>
constexpr decltype(auto) invoke(F&& f, Args&&... args);
template <class T>
T&& declval() noexcept;
template <class F, class... Args>
using invoke_result_t = decltype(invoke(declval<F>(), declval<Args>()...));
template <class T>
constexpr bool is_object_v = true;
} // namespace std

namespace demo {

// \rSec3[box.class]{Class template box}

//! A body-local alias the description introduces with "Let" is not repeated
//! in the Effects code (issue #404).
template <class T>
class box {
  public:
    // \ref{box.ops}, operations
    template <class F>
    constexpr auto apply(F&& f) const;

  private:
    //! \expos(val)
    T* value_ = nullptr;
};

// \rSec3[box.ops]{Operations}

//! Let `U` be `invoke_result_t<F, T&>`.
//! \effects-equiv
template <class T>
template <class F>
constexpr auto box<T>::apply(F&& f) const {
    using U = std::invoke_result_t<F, T&>;
    static_assert(std::is_object_v<U>);
    return std::invoke(std::forward<F>(f), *value_);
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_LET_ALIAS_HPP
