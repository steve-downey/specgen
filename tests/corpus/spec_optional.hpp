// tests/corpus/spec_optional.hpp                                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The acid-test corpus (design §10): a representative subset of
// `beman::optional`'s primary template, reverse-engineered into specgen's
// authoring style, whose generated synopsis is diffed against the draft's
// [optional] synopsis. Modelled on
// bemanproject/optional's include/beman/optional/optional.hpp — not vendored
// (decision hermetic-corpus), so the `std` names below are local stand-ins
// declared just well enough to resolve; only their *resolution* matters to the
// namespace rewriter. Nothing here is ever instantiated, so the bodies need
// only parse.
//
// It exercises every path the tool has: the §10 golden trio — `emplace`
// (Mandates from a static_assert + authored Effects), `value_or` (Mandates +
// `\effects-equiv`), and a converting constructor (a long Constraints list) —
// plus `\merge`d defaulted twins, exposition-only storage, unmarked private
// helpers that must not reach the synopsis, a hidden friend, and a `\seebelow`
// deduced return type.
//
// `nullopt_t`'s constructor carries `\omit` because the draft does not specify
// it: it exists only to make the type non-aggregate and not default
// constructible, which [optional.nullopt]'s prose states directly rather than
// by giving the constructor wording of its own.
//
// The default constructor's body calls `hard_reset()` — an undocumented
// private helper — on purpose, and it is the one thing here that draws a
// finding. Design §9 gives that its *note* severity: the body carries no
// `\effects-equiv`, so nothing the reader sees names `hard_reset`, and the
// wording is followable as written. It is the same helper §9 names in its
// *error* example, in the one position where the error does not apply; the
// note is pinned by `golden.optional_validate`.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_OPTIONAL_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_OPTIONAL_HPP

namespace std {
template <class T>
using remove_cv_t = T;
template <class T>
constexpr bool is_copy_constructible_v = true;
template <class T>
constexpr bool is_trivially_copy_constructible_v = true;
template <class T>
constexpr bool is_trivially_destructible_v = true;
template <class T, class... Args>
constexpr bool is_constructible_v = true;
template <class T, class U>
constexpr bool is_convertible_v = true;
template <class T, class U>
constexpr bool is_same_v = false;
} // namespace std

namespace beman::optional {

struct nullopt_t {
    //! \omit
    explicit constexpr nullopt_t(int) {}
};
inline constexpr nullopt_t nullopt{0};

struct in_place_t {
    explicit in_place_t() = default;
};
inline constexpr in_place_t in_place{};

template <class T>
class optional {
  public:
    using value_type = T;

    // \ref{optional.ctor}, constructors
    constexpr optional() noexcept;
    constexpr optional(nullopt_t) noexcept;

    constexpr optional(const optional& rhs)
        requires std::is_copy_constructible_v<T> && (!std::is_trivially_copy_constructible_v<T>);

    //! \merge
    constexpr optional(const optional&)
        requires std::is_copy_constructible_v<T> && std::is_trivially_copy_constructible_v<T>
    = default;

    template <class... Args>
    constexpr explicit optional(in_place_t, Args&&... args)
        requires std::is_constructible_v<T, Args...>;

    template <class U>
    constexpr explicit(!std::is_convertible_v<U, T>) optional(const optional<U>& rhs)
        requires std::is_constructible_v<T, const U&> && std::is_convertible_v<U, T> && (!std::is_same_v<T, U>) &&
                 (!std::is_constructible_v<T, optional<U>>);

    // \ref{optional.dtor}, destructor
    //! \merge
    constexpr ~optional()
        requires std::is_trivially_destructible_v<T>
    = default;

    // \ref{optional.assign}, assignment
    template <class... Args>
    constexpr T& emplace(Args&&... args);

    // \ref{optional.observe}, observers
    constexpr bool     has_value() const noexcept;
    constexpr const T& operator*() const&;

    template <class U = std::remove_cv_t<T>>
    constexpr std::remove_cv_t<T> value_or(U&& u) const&;

    // \ref{optional.monadic}, monadic operations
    template <class F>
    constexpr auto transform(F&& f) const&;

    // \ref{optional.mod}, modifiers
    constexpr void reset() noexcept;

    //! \returns `true` if both operands are disengaged, or both are engaged
    //! with equal contained values.
    friend constexpr bool operator==(const optional& x, const optional& y) { return x.engaged_ == y.engaged_; }

  private:
    //! \expos
    T value_;
    //! \expos
    bool engaged_ = false;

    constexpr void construct(const T& v);
    constexpr void hard_reset() noexcept;
};

// \rSec3[optional.ctor]{Constructors}

//! \ensures `*this` does not contain a value.
template <class T>
constexpr optional<T>::optional() noexcept {
    hard_reset();
}

//! \also
template <class T>
constexpr optional<T>::optional(nullopt_t) noexcept {}

//! \effects If `rhs` contains a value, direct-non-list-initializes the
//! contained value with `*rhs`.
//! \ensures `has_value()` is equal to `rhs.has_value()`.
template <class T>
constexpr optional<T>::optional(const optional& rhs)
    requires std::is_copy_constructible_v<T> && (!std::is_trivially_copy_constructible_v<T>)
{
    engaged_ = rhs.engaged_;
}

//! \effects Direct-non-list-initializes the contained value with
//! `std::forward<Args>(args)...`.
//! \ensures `has_value()` is `true`.
template <class T>
template <class... Args>
constexpr optional<T>::optional(in_place_t, Args&&... args)
    requires std::is_constructible_v<T, Args...>
{
    engaged_ = true;
}

//! \effects If `rhs` contains a value, direct-non-list-initializes the
//! contained value with `*rhs`.
//! \ensures `has_value()` is equal to `rhs.has_value()`.
template <class T>
template <class U>
constexpr optional<T>::optional(const optional<U>& rhs)
    requires std::is_constructible_v<T, const U&> && std::is_convertible_v<U, T> && (!std::is_same_v<T, U>) &&
             (!std::is_constructible_v<T, optional<U>>)
{
    engaged_ = rhs.has_value();
}

// \rSec3[optional.assign]{Assignment}

//! \effects Destroys any contained value, then direct-non-list-initializes the
//! contained value with `std::forward<Args>(args)...`.
//! \ensures `has_value()` is `true`.
//! \returns A reference to the new contained value.
template <class T>
template <class... Args>
constexpr T& optional<T>::emplace(Args&&... args) {
    static_assert(std::is_constructible_v<T, Args...>);
    engaged_ = true;
    return value_;
}

// \rSec3[optional.observe]{Observers}

//! \returns `true` if and only if `*this` contains a value.
template <class T>
constexpr bool optional<T>::has_value() const noexcept {
    return engaged_;
}

//! \hardexpects `*this` contains a value.
//! \returns A reference to the contained value.
template <class T>
constexpr const T& optional<T>::operator*() const& {
    return value_;
}

//! \effects-equiv
template <class T>
template <class U>
constexpr std::remove_cv_t<T> optional<T>::value_or(U&& u) const& {
    static_assert(std::is_copy_constructible_v<T> && std::is_convertible_v<U, T>);
    return has_value() ? **this : static_cast<std::remove_cv_t<T>>(u);
}

// \rSec3[optional.monadic]{Monadic operations}

//! \seebelow
//! \effects If `*this` contains a value, returns an optional holding the result
//! of invoking `f` with the contained value; otherwise returns an empty
//! optional.
//! \remarks The return type is `remove_cvref_t<invoke_result_t<F, const T&>>`.
template <class T>
template <class F>
constexpr auto optional<T>::transform(F&& f) const& {
    return f(value_);
}

// \rSec3[optional.mod]{Modifiers}

//! \effects-equiv
//! \ensures `has_value()` is `false`.
template <class T>
constexpr void optional<T>::reset() noexcept {
    engaged_ = false;
}

} // namespace beman::optional

#endif // BEMAN_SPECGEN_CORPUS_SPEC_OPTIONAL_HPP
