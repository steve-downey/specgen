// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/tree_algorithms/functor.hpp>
#include <beman/tree_algorithms/functor.hpp> // Re-inclusion: verifies include guard

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>
#include <type_traits>

using beman::tree_algorithms::functor_typeclass;
using beman::tree_algorithms::has_functor_instance;

namespace functor_test {

// Toy functor: a homogeneous pair. The two positions are distinguishable, so
// a wrong element order (or an fmap applied to only one position) produces a
// wrong answer.

template <typename A>
struct PairF {
    A first;
    A second;

    friend constexpr auto operator==(const PairF&, const PairF&) -> bool = default;
};

template <typename A>
struct PairFFunctorImpl {
    template <typename Fn>
    constexpr auto fmap(this auto&&, Fn&& fn, const PairF<A>& value) {
        using B = std::remove_cvref_t<std::invoke_result_t<Fn, const A&>>;
        return PairF<B>{std::invoke(fn, value.first), std::invoke(fn, value.second)};
    }
};

template <typename A>
struct PairFFunctorMap : beman::tree_algorithms::Functor<PairFFunctorImpl<A>> {
    using PairFFunctorImpl<A>::fmap;
};

} // namespace functor_test

namespace beman::tree_algorithms {

/** Functor instance for PairF<A>, keyed on the concrete type. */
template <typename A>
inline constexpr auto functor_typeclass<functor_test::PairF<A>> = functor_test::PairFFunctorMap<A>{};

} // namespace beman::tree_algorithms

namespace {

using functor_test::PairF;

// Instance detection: the specialization above is found, the std::false_type
// default remains for unadapted types.
static_assert(has_functor_instance<PairF<int>>);
static_assert(!has_functor_instance<int>);
static_assert(!has_functor_instance<PairF<int>*>);

// ---------------------------------------------------------------------
// Constexpr coverage (DEV-04): both the fmap primitive and the derived
// replace evaluate in constant expressions, through the lookup object.
// ---------------------------------------------------------------------

// fmap: position-sensitive check — {1, 2} maps to {10, 20}; a swapped or
// duplicated position gives {20, 10}, {10, 10}, or {20, 20} instead.
constexpr auto fmap_preserves_positions() -> bool {
    const auto& map = functor_typeclass<PairF<int>>;
    return map.fmap([](int x) { return x * 10; }, PairF<int>{1, 2}) == PairF<int>{10, 20};
}

static_assert(fmap_preserves_positions());

// replace (derived from fmap in the CRTP base): every element replaced.
constexpr auto replace_fills_both_positions() -> bool {
    const auto& map = functor_typeclass<PairF<int>>;
    return map.replace(PairF<int>{1, 2}, 7) == PairF<int>{7, 7};
}

static_assert(replace_fills_both_positions());

} // namespace

TEST_CASE("Functor - FmapThroughLookupObject", "[tree_algorithms::functor]") {
    const auto& map = functor_typeclass<PairF<int>>;

    // Non-idempotent function on asymmetric input: order or arity mistakes
    // in fmap change the result.
    auto doubled = map.fmap([](int x) { return x + x + 1; }, PairF<int>{3, 5});
    CHECK(doubled == PairF<int>{7, 11});
}

TEST_CASE("Functor - FmapChangesElementType", "[tree_algorithms::functor]") {
    const auto& map = functor_typeclass<PairF<int>>;

    auto shown = map.fmap([](int x) { return std::to_string(x); }, PairF<int>{4, 2});
    CHECK(shown == PairF<std::string>{"4", "2"});
}

TEST_CASE("Functor - ReplaceIsDerivedFromFmap", "[tree_algorithms::functor]") {
    const auto& map = functor_typeclass<PairF<int>>;

    CHECK(map.replace(PairF<int>{1, 2}, 9) == PairF<int>{9, 9});

    // replace may change the element type, exactly as fmap can.
    auto named = map.replace(PairF<int>{1, 2}, std::string{"x"});
    CHECK(named == PairF<std::string>{"x", "x"});
}
