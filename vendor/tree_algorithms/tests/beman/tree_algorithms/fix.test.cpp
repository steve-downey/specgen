// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/tree_algorithms/fix.hpp>
#include <beman/tree_algorithms/fix.hpp> // Re-inclusion: verifies include guard

#include <beman/tree_algorithms/box.hpp>

#include <catch2/catch_test_macros.hpp>

#include <variant>

using beman::tree_algorithms::Box;
using beman::tree_algorithms::Fix;
using beman::tree_algorithms::make_box;
using beman::tree_algorithms::unwrap_fix;
using beman::tree_algorithms::wrap_fix;

namespace {

struct Zero {};

template <typename A>
struct Succ {
    Box<A> pred;
};

template <typename A>
using NatF = std::variant<Zero, Succ<A>>;

// Constexpr coverage: Fix construction and wrap/unwrap round-trip evaluate
// at compile time, including a Box-ed recursive position.
constexpr auto fix_wrap_unwrap_roundtrip() -> bool {
    using Nat = Fix<NatF>;
    auto zero = wrap_fix<NatF>(NatF<Nat>{Zero{}});
    return std::holds_alternative<Zero>(unwrap_fix(zero));
}

static_assert(fix_wrap_unwrap_roundtrip());

constexpr auto fix_succ_roundtrip() -> bool {
    using Nat = Fix<NatF>;
    auto zero = wrap_fix<NatF>(NatF<Nat>{Zero{}});
    auto one  = wrap_fix<NatF>(NatF<Nat>{Succ<Nat>{make_box<Nat>(zero)}});

    const auto& layer1 = unwrap_fix(one);
    if (!std::holds_alternative<Succ<Nat>>(layer1))
        return false;
    const auto& layer0 = unwrap_fix(*std::get<Succ<Nat>>(layer1).pred);
    return std::holds_alternative<Zero>(layer0);
}

static_assert(fix_succ_roundtrip());

} // namespace

TEST_CASE("Fix - NatFZero", "[tree_algorithms::fix]") {
    using Nat         = Fix<NatF>;
    auto        zero  = wrap_fix<NatF>(NatF<Nat>{Zero{}});
    const auto& layer = unwrap_fix(zero);
    CHECK(std::holds_alternative<Zero>(layer));
}

TEST_CASE("Fix - NatFSucc", "[tree_algorithms::fix]") {
    using Nat = Fix<NatF>;
    auto zero = wrap_fix<NatF>(NatF<Nat>{Zero{}});
    auto one  = wrap_fix<NatF>(NatF<Nat>{Succ<Nat>{make_box<Nat>(zero)}});
    auto two  = wrap_fix<NatF>(NatF<Nat>{Succ<Nat>{make_box<Nat>(one)}});

    const auto& layer2 = unwrap_fix(two);
    REQUIRE(std::holds_alternative<Succ<Nat>>(layer2));

    const auto& layer1 = unwrap_fix(*std::get<Succ<Nat>>(layer2).pred);
    REQUIRE(std::holds_alternative<Succ<Nat>>(layer1));

    const auto& layer0 = unwrap_fix(*std::get<Succ<Nat>>(layer1).pred);
    CHECK(std::holds_alternative<Zero>(layer0));
}

TEST_CASE("Fix - WrapUnwrapRoundTrip", "[tree_algorithms::fix]") {
    using Nat = Fix<NatF>;
    NatF<Nat>   layer{Zero{}};
    auto        fixed     = wrap_fix<NatF>(layer);
    const auto& recovered = unwrap_fix(fixed);
    CHECK(std::holds_alternative<Zero>(recovered));
}
