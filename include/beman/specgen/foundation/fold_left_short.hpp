// include/beman/specgen/foundation/fold_left_short.hpp          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Provenance: ported from compile-time-scheme
//   repo:   https://github.com/steve-downey/compile-time-scheme (commit f60b0ff)
//   path:   src/smd/cl/foundation/fold_left_short.hpp
// Adapted for specgen: namespace renamed to beman::specgen::foundation. The body
// is carried as-is — it is written against any short_circuit_effect, which
// std::expected models (specgen's carrier; see foundation/DIVERGENCES.md), so
// no rewrite against a specific carrier was needed.
// (decision expected-error-taxonomy)
#ifndef BEMAN_SPECGEN_FOUNDATION_FOLD_LEFT_SHORT_HPP
#define BEMAN_SPECGEN_FOUNDATION_FOLD_LEFT_SHORT_HPP

#include <concepts>
#include <functional>
#include <ranges>
#include <type_traits>
#include <utility>

namespace beman::specgen::foundation {

/// An effect type a fold can stop on: it can be asked whether it succeeded,
/// and carries a value on success and an error otherwise. std::expected<T, E>
/// models this (decision expected-error-taxonomy: expected is the carrier).
template <class E>
concept short_circuit_effect = requires(const E& e) {
    { e.has_value() } -> std::convertible_to<bool>;
    e.value();
    e.error();
};

/// Short-circuiting left fold (decision expected-error-taxonomy: the early-stop verb).
///
/// Applies @p f to an accumulator and each element of @p range in order,
/// where each step returns an effect (e.g. @c std::expected<Acc, E>). The first
/// failed step is returned immediately and no later element is visited — this
/// early exit is the difference from @c std::ranges::fold_left and from
/// @c traverse, both of which visit every element.
///
/// @tparam Range An input range.
/// @tparam Acc   Accumulator type.
/// @tparam F     Callable with signature @c Effect(Acc, Element) where
///               @c Effect models @ref short_circuit_effect and is
///               constructible from @c Acc.
/// @param  range The elements to fold, visited left to right.
/// @param  init  The initial accumulator value.
/// @param  f     The effectful step function.
/// @return The first failed step's effect, or a successful effect holding
///         the final accumulator.
template <std::ranges::input_range Range, class Acc, class F>
    requires short_circuit_effect<
                 std::remove_cvref_t<std::invoke_result_t<F&, Acc, std::ranges::range_reference_t<const Range>>>> &&
             std::constructible_from<
                 std::remove_cvref_t<std::invoke_result_t<F&, Acc, std::ranges::range_reference_t<const Range>>>,
                 Acc>
constexpr auto fold_left_short(const Range& range, Acc init, F f) {
    using effect_type =
        std::remove_cvref_t<std::invoke_result_t<F&, Acc, std::ranges::range_reference_t<const Range>>>;
    for (const auto& element : range) { // substrate generic algorithm
        auto step = std::invoke(f, std::move(init), element);
        if (!step.has_value()) {
            return step;
        }
        init = step.value();
    }
    return effect_type{std::move(init)};
}

} // namespace beman::specgen::foundation

#endif // BEMAN_SPECGEN_FOUNDATION_FOLD_LEFT_SHORT_HPP
