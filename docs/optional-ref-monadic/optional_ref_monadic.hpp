// docs/optional-ref-monadic/optional_ref_monadic.hpp               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The monadic operations of optional<T&>, [optional.ref.monadic], taken from
// Beman optional (include/beman/optional/optional.hpp) with everything else
// in the class left out. The function bodies, their static_asserts and the
// requires-clause are the library's; the `//!` lines are the specgen markup.

#ifndef BEMAN_SPECGEN_DOCS_OPTIONAL_REF_MONADIC_HPP
#define BEMAN_SPECGEN_DOCS_OPTIONAL_REF_MONADIC_HPP

#include <concepts>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace beman::optional {

//! \elsewhere
struct in_place_t {
    explicit in_place_t() = default;
};

//! \elsewhere
struct nullopt_t {
    explicit constexpr nullopt_t(int) noexcept {}
};

template <class T>
class optional;

namespace detail {
template <class T>
inline constexpr bool is_optional = false;
template <class T>
inline constexpr bool is_optional<optional<T>> = true;

//! \omit
struct from_function_t {
    explicit from_function_t() = default;
};
inline constexpr from_function_t from_function{};
} // namespace detail

template <class T>
class optional<T&> {
  public:
    //! \omit
    constexpr optional() noexcept = default;

    //! \elsewhere
    constexpr bool has_value() const noexcept { return value_ != nullptr; }

    // \ref{optional.ref.monadic}, monadic operations
    template <class F>
    constexpr auto and_then(F&& f) const;

    template <class F>
    constexpr optional<std::invoke_result_t<F, T&>> transform(F&& f) const;

    template <class F>
    constexpr optional or_else(F&& f) const
        requires(std::invocable<F>);

  private:
    //! \expos(val)
    T* value_ = nullptr;

    //! \omit
    template <class U>
    friend class optional;

    //! \omit
    template <class F, class Arg>
    constexpr optional(detail::from_function_t, F&& f, Arg&& arg) {
        T& r(std::invoke(std::forward<F>(f), std::forward<Arg>(arg)));
        value_ = std::addressof(r);
    }
};

// \rSec3[optional.ref.monadic]{Monadic operations}

//! Let `U` be `invoke_result_t<F, T&>`.
//! \mandates `U` is a specialization of `optional`.
//! \covers detail::is_optional<U>
//! \effects-equiv
template <class T>
template <class F>
constexpr auto optional<T&>::and_then(F&& f) const {
    using U = std::invoke_result_t<F, T&>;
    static_assert(detail::is_optional<U>, "F must return an optional");
    if (has_value()) {
        return std::invoke(std::forward<F>(f), *value_);
    } else {
        return std::remove_cvref_t<U>();
    }
}

//! Let `U` be `invoke_result_t<F, T&>`.
//! \mandates `U` is a valid contained type for `optional`.
//! \covers !is_same_v<remove_cvref_t<U>, in_place_t>
//! \covers !is_same_v<remove_cvref_t<U>, nullopt_t>
//! \covers (is_object_v<U> && !is_array_v<U>) || is_lvalue_reference_v<U>
//! The declaration
//! \codeblock
//! U u(invoke(std::forward<F>(f), *@\exposid{val}@));
//! \endcodeblock
//! is well-formed for some invented variable `u`.
//! \note There is no requirement that `U` is movable\iref{dcl.init.general}.
//! \endnote
//! \returns If `*this` contains a value, an `optional<U>` object
//! whose contained value is direct-non-list-initialized with
//! `invoke(std::forward<F>(f), *val)`;
//! otherwise, `optional<U>()`.
template <class T>
template <class F>
constexpr optional<std::invoke_result_t<F, T&>> optional<T&>::transform(F&& f) const {
    using U = std::invoke_result_t<F, T&>;
    static_assert(!std::is_same_v<std::remove_cvref_t<U>, in_place_t>, "Result must not be in_place_t");
    static_assert(!std::is_same_v<std::remove_cvref_t<U>, nullopt_t>, "Result must not be nullopt_t");
    static_assert((std::is_object_v<U> && !std::is_array_v<U>) || std::is_lvalue_reference_v<U>,
                  "Result must be an non-array object or an lvalue reference");
    if (has_value()) {
        return optional<U>{detail::from_function, std::forward<F>(f), *value_};
    } else {
        return optional<U>{};
    }
}

//! \effects-equiv
template <class T>
template <class F>
constexpr optional<T&> optional<T&>::or_else(F&& f) const
    requires(std::invocable<F>)
{
    static_assert(std::is_same_v<std::remove_cvref_t<std::invoke_result_t<F>>, optional>,
                  "Result must be an optional");
    if (has_value()) {
        return *this;
    } else {
        return std::forward<F>(f)();
    }
}

} // namespace beman::optional

#endif // BEMAN_SPECGEN_DOCS_OPTIONAL_REF_MONADIC_HPP
