// include/beman/specgen/foundation/enumeration.hpp                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef BEMAN_SPECGEN_FOUNDATION_ENUMERATION_HPP
#define BEMAN_SPECGEN_FOUNDATION_ENUMERATION_HPP

// Enumerations as sum types (decision visitation-rules).
//
// `overloaded` makes a forgotten variant alternative a compile error that
// names the case. This header does the same for the other sum type in the
// tree, the enumeration, so a `switch` over one is never needed:
//
//   - `enumerators<E>` states an enumeration's enumerators once, as data,
//     specialized beside the enumeration;
//   - `table<E>(is<E::A>(x), is<E::B>(y), ...)` is a total lookup from
//     enumerator to value. A case missing from it fails to compile, and the
//     diagnostic names the missing enumerator in its template argument;
//   - `match(e, when<E::A>(f), ...)` runs the case for `e` and fails the same
//     way when a case is missing.
//
// Neither has a fallback, so nothing answers in a missing case's place: there
// is no code path that could, which a `return "visible";` after a `switch`
// always was.
//
// What a specialization cannot check is the list itself against the
// enumeration: a new enumerator absent from `enumerators<E>` is invisible
// here. Where the compiler has reflection (`__cpp_impl_reflection`), the
// check is made; everywhere else `-Werror=switch-enum` on the one place an
// enumerator list is written down is not available, so the list sits directly
// under its enumeration, where an author adding an enumerator is looking.

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

#if defined(__cpp_impl_reflection) && __has_include(<meta>)
    #include <meta>
#endif

namespace beman::specgen::foundation {

// The enumerators of E, in declaration order. Specialize beside the
// enumeration:
//
//   template <> inline constexpr auto enumerators<Severity> =
//       std::array{Severity::Error, Severity::Warning, Severity::Note};
template <class E>
inline constexpr auto enumerators = std::false_type{};

namespace enumeration_detail {

template <class E>
concept listed = std::is_enum_v<E> && requires { enumerators<E>.size(); };

// Instantiating this names the enumerator in the diagnostic's template
// argument, the way `overloaded`'s catch-all names the alternative.
template <auto Enumerator, int Handled>
struct enumerator_handled_once {
    static_assert(Handled != 0, "enumeration: unhandled enumerator -- add a case for the one named in this type");
    static_assert(Handled < 2,
                  "enumeration: enumerator handled twice -- remove one case for the one named in this type");
};

#if defined(__cpp_impl_reflection) && __has_include(<meta>)
// The list is the enumeration: no enumerator of E is missing from it.
template <class E>
consteval bool list_is_complete() {
    return std::meta::enumerators_of(^^E).size() == enumerators<E>.size();
}
#endif

template <auto Enumerator, auto... Handled>
inline constexpr int times_handled = (0 + ... + int(Enumerator == Handled));

// Every enumerator of E is named by exactly one of `Handled`.
template <class E, auto... Handled>
constexpr void require_complete() {
    static_assert(listed<E>, "enumeration: specialize foundation::enumerators for this enumeration");
    if constexpr (listed<E>) {
#if defined(__cpp_impl_reflection) && __has_include(<meta>)
        static_assert(list_is_complete<E>(), "enumeration: enumerators<E> lacks an enumerator of E");
#endif
        []<std::size_t... I>(std::index_sequence<I...>) {
            (void(enumerator_handled_once<enumerators<E>[I], times_handled<enumerators<E>[I], Handled...>>{}), ...);
        }(std::make_index_sequence<enumerators<E>.size()>{});
    }
}

} // namespace enumeration_detail

// One case: the enumerator it is for, and a value (for `table`) or a callable
// (for `match`).
template <auto Enumerator, class Value>
struct enumerator_case {
    static constexpr auto enumerator = Enumerator;
    Value                 value;
};

template <auto Enumerator, class Value>
constexpr auto is(Value&& value) {
    return enumerator_case<Enumerator, std::decay_t<Value>>{std::forward<Value>(value)};
}

template <auto Enumerator, class Callable>
constexpr auto when(Callable&& callable) {
    return enumerator_case<Enumerator, std::decay_t<Callable>>{std::forward<Callable>(callable)};
}

// A total map from the enumerators of E to Value.
template <class E, class Value>
class total_table {
  public:
    template <auto... Enumerators>
    constexpr explicit total_table(enumerator_case<Enumerators, Value>... cases) {
        enumeration_detail::require_complete<E, Enumerators...>();
        ((values_[position(Enumerators)] = std::move(cases.value)), ...);
    }

    constexpr const Value& operator[](E e) const { return values_[position(e)]; }

  private:
    static constexpr std::size_t position(E e) {
        return static_cast<std::size_t>(std::ranges::find(enumerators<E>, e) - enumerators<E>.begin());
    }

    std::array<Value, enumerators<E>.size()> values_{};
};

template <class E, auto First, class Value, auto... Rest>
constexpr auto table(enumerator_case<First, Value> first, enumerator_case<Rest, Value>... rest) {
    static_assert(std::is_same_v<E, decltype(First)>, "table: the cases are for another enumeration");
    return total_table<E, Value>(std::move(first), std::move(rest)...);
}

namespace enumeration_detail {

template <std::size_t I, class R, class E, class Tuple>
constexpr R dispatch(E e, Tuple& cases) {
    auto& current = std::get<I>(cases);
    if constexpr (I + 1 == std::tuple_size_v<Tuple>) {
        if (e == current.enumerator)
            return std::invoke(current.value);
        std::unreachable();
    } else {
        if (e == current.enumerator)
            return std::invoke(current.value);
        return dispatch<I + 1, R>(e, cases);
    }
}

} // namespace enumeration_detail

// Run the case for `e`. Every case must return the same type, and every
// enumerator must have a case; an `e` outside the enumeration is undefined.
template <class E, auto... Enumerators, class... Callables>
constexpr auto match(E e, enumerator_case<Enumerators, Callables>... cases) {
    static_assert((std::is_same_v<E, decltype(Enumerators)> && ...), "match: the cases are for another enumeration");
    enumeration_detail::require_complete<E, Enumerators...>();
    using result = std::common_type_t<std::invoke_result_t<Callables&>...>;
    auto bundle  = std::tuple<enumerator_case<Enumerators, Callables>...>{std::move(cases)...};
    return enumeration_detail::dispatch<0, result>(e, bundle);
}

} // namespace beman::specgen::foundation

#endif // BEMAN_SPECGEN_FOUNDATION_ENUMERATION_HPP
