// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/tree_algorithms/functors.hpp>
#include <beman/tree_algorithms/functors.hpp> // Re-inclusion: verifies include guard

#include <beman/tree_algorithms/box.hpp>
#include <beman/tree_algorithms/overloaded.hpp>
#include <beman/tree_algorithms/recursion_schemes.hpp>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <type_traits>
#include <utility>
#include <variant>

using beman::tree_algorithms::fold_fix;
using beman::tree_algorithms::has_functor_instance;
using beman::tree_algorithms::make_slot;
using beman::tree_algorithms::make_succ;
using beman::tree_algorithms::make_zero;
using beman::tree_algorithms::Nat;
using beman::tree_algorithms::nat_from_int;
using beman::tree_algorithms::nat_to_int;
using beman::tree_algorithms::NatF;
using beman::tree_algorithms::overloaded;
using beman::tree_algorithms::refold;
using beman::tree_algorithms::Succ;
using beman::tree_algorithms::unfold_fix;
using beman::tree_algorithms::Zero;

namespace {

// The NatF family from functors.hpp through all three verbs, via both the
// lookup API and the explicit-fmap API.

// Hand-rolled explicit fmap, independent of the header's functor instance.
template <typename A, typename Fn>
constexpr auto fmap_nat(Fn&& fn, const NatF<A>& layer) {
    using B = std::remove_cvref_t<std::invoke_result_t<Fn, const A&>>;
    return std::visit(
        overloaded{
            [](const Zero&) -> NatF<B> { return Zero{}; },
            [&fn](const Succ<A>& s) -> NatF<B> { return Succ<B>{make_slot<B>(std::invoke(fn, *s.pred))}; },
        },
        layer);
}

inline constexpr auto fmap_nat_fn = [](auto&& fn, const auto& layer) {
    return fmap_nat(std::forward<decltype(fn)>(fn), layer);
};

// Non-idempotent algebra (DEV-01): g(x) = 2x + 1 per Succ layer. Applied
// exactly n times to 0 it gives 2^n - 1; one extra or missing application
// gives a different power-of-two neighborhood, so miscounted layers are
// unmissable (count_algebra alone would drift by only 1).
inline constexpr auto weigh_algebra = [](const NatF<int>& layer) -> int {
    return std::visit(overloaded{
                          [](const Zero&) { return 0; },
                          [](const Succ<int>& s) { return 2 * *s.pred + 1; },
                      },
                      layer);
};

inline constexpr auto countdown_coalgebra = [](int n) -> NatF<int> {
    if (n <= 0)
        return Zero{};
    return Succ<int>{make_slot<int>(n - 1)};
};

// ---------------------------------------------------------------------
// Constexpr coverage (DEV-04): each verb through each API in a constant
// expression.
// ---------------------------------------------------------------------

static_assert(has_functor_instance<NatF<int>>);
static_assert(has_functor_instance<NatF<Nat>>);
static_assert(!has_functor_instance<int>);

// Header converters are built on the lookup verbs: round trip at compile
// time.
static_assert(nat_to_int(nat_from_int(0)) == 0);
static_assert(nat_to_int(nat_from_int(5)) == 5);

// fold_fix, both APIs: three Succ layers weigh 2^3 - 1 == 7.
constexpr auto weigh_three() -> std::pair<int, int> {
    auto three = make_succ(make_succ(make_succ(make_zero())));
    return {fold_fix<int>(weigh_algebra, three), fold_fix<int>(weigh_algebra, fmap_nat_fn, three)};
}

static_assert(weigh_three().first == 7);
static_assert(weigh_three().second == 7);

// unfold_fix, both APIs: countdown from 4, weighed: 2^4 - 1 == 15.
static_assert(fold_fix<int>(weigh_algebra, unfold_fix<NatF>(countdown_coalgebra, 4)) == 15);
static_assert(fold_fix<int>(weigh_algebra, unfold_fix<NatF>(countdown_coalgebra, fmap_nat_fn, 4)) == 15);

// refold, both APIs: fused pipeline agrees.
static_assert(refold<int, NatF>(weigh_algebra, countdown_coalgebra, 4) == 15);
static_assert(refold<int, NatF>(weigh_algebra, countdown_coalgebra, fmap_nat_fn, 4) == 15);

} // namespace

TEST_CASE("Functors - NatRoundTrip", "[tree_algorithms::functors]") {
    for (int n = 0; n < 10; ++n) {
        CHECK(nat_to_int(nat_from_int(n)) == n);
    }
}

TEST_CASE("Functors - SmartConstructorsAgreeWithUnfold", "[tree_algorithms::functors]") {
    auto by_hand = make_succ(make_succ(make_succ(make_zero())));
    CHECK(nat_to_int(by_hand) == 3);
    CHECK(fold_fix<int>(weigh_algebra, by_hand) == fold_fix<int>(weigh_algebra, nat_from_int(3)));
}

TEST_CASE("Functors - WeighDiscriminatesLayerCount", "[tree_algorithms::functors]") {
    // DEV-01: 2x + 1 applied per layer. Any drift in the number of algebra
    // applications breaks the 2^n - 1 sequence.
    for (int n = 0; n < 8; ++n) {
        CHECK(fold_fix<int>(weigh_algebra, nat_from_int(n)) == (1 << n) - 1);
    }
}

TEST_CASE("Functors - RefoldMatchesFoldOfUnfoldOnBothApis", "[tree_algorithms::functors]") {
    for (int n = 0; n < 8; ++n) {
        auto materialized = fold_fix<int>(weigh_algebra, unfold_fix<NatF>(countdown_coalgebra, n));
        CHECK(refold<int, NatF>(weigh_algebra, countdown_coalgebra, n) == materialized);
        CHECK(refold<int, NatF>(weigh_algebra, countdown_coalgebra, fmap_nat_fn, n) == materialized);
    }
}
