// include/beman/specgen/foundation/traverse.hpp                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Concrete traverse / sequence overloads for threading a fallible effect
// through a structure (decision expected-error-taxonomy). Deliberately NOT a
// Traversable framework (decision no-typeclass-objects): specgen has a
// handful of functor-shaped types, so each is a named overload rather than a
// typeclass. This header covers std::vector over std::expected (specgen's
// carrier; see foundation/DIVERGENCES.md).
#ifndef BEMAN_SPECGEN_FOUNDATION_TRAVERSE_HPP
#define BEMAN_SPECGEN_FOUNDATION_TRAVERSE_HPP

#include <expected>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

namespace beman::specgen::foundation {

/// sequence: turn a vector of effects into an effect of a vector, fail-fast.
///
/// structure-of-expected -> expected-of-structure. On the first unexpected the
/// error is returned and the remaining elements are not collected; otherwise a
/// vector of every value is returned in order.
template <typename T, typename E>
constexpr auto sequence(const std::vector<std::expected<T, E>>& xs) -> std::expected<std::vector<T>, E> {
    std::vector<T> out;
    out.reserve(xs.size());
    for (const auto& effect : xs) { // substrate generic algorithm
        if (!effect.has_value()) {
            return std::unexpected(effect.error());
        }
        out.push_back(effect.value());
    }
    return out;
}

/// traverse: map @p f over @p xs, collecting the values, fail-fast on the first
/// error. Equivalent to sequence(xs | transform(f)) but without materializing
/// the intermediate vector of effects.
///
/// @param f callable T -> std::expected<U, E>.
template <typename T,
          typename F,
          typename R = std::remove_cvref_t<std::invoke_result_t<F&, const T&>>,
          typename U = typename R::value_type,
          typename E = typename R::error_type>
constexpr auto traverse(const std::vector<T>& xs, F f) -> std::expected<std::vector<U>, E> {
    std::vector<U> out;
    out.reserve(xs.size());
    for (const auto& element : xs) { // substrate generic algorithm
        auto step = std::invoke(f, element);
        if (!step.has_value()) {
            return std::unexpected(std::move(step).error());
        }
        out.push_back(std::move(step).value());
    }
    return out;
}

} // namespace beman::specgen::foundation

#endif // BEMAN_SPECGEN_FOUNDATION_TRAVERSE_HPP
