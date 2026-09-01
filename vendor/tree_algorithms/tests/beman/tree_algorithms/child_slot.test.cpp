// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/tree_algorithms/child_slot.hpp>
#include <beman/tree_algorithms/child_slot.hpp> // re-inclusion / idempotency check

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <type_traits>

using beman::tree_algorithms::Box;
using beman::tree_algorithms::child_slot_t;
using beman::tree_algorithms::Fix;
using beman::tree_algorithms::make_slot;

namespace {

// A minimal base functor to exercise the knot case.
template <typename A>
struct PairF {
    child_slot_t<A> left;
    child_slot_t<A> right;
};

using PairFix = Fix<PairF>;

} // namespace

TEST_CASE("child_slot: header is idempotent") {
    // Bootstrap: passes if the file compiles and links.
    REQUIRE(true);
}

// The selection rule, pinned at compile time: inline at complete types,
// Box exactly at the fixed point.
static_assert(std::is_same_v<child_slot_t<int>, std::optional<int> >);
static_assert(std::is_same_v<child_slot_t<std::string>, std::optional<std::string> >);
static_assert(std::is_same_v<child_slot_t<PairFix>, Box<PairFix> >);

// The knot instantiates: PairF<Fix<PairF>> holds its incomplete children
// behind Box, so Fix closes.
static_assert(sizeof(PairFix) > 0);

// make_slot is constexpr and the inline branch allocates nothing.
static_assert(*make_slot<int>(42) == 42);
static_assert(!child_slot_t<int>{});

TEST_CASE("child_slot: inline slots hold values, disengage by default") {
    child_slot_t<int> absent;
    CHECK(!absent);

    auto engaged = make_slot<int>(7);
    REQUIRE(engaged);
    CHECK(*engaged == 7);

    auto text = make_slot<std::string>(3U, 'x');
    REQUIRE(text);
    CHECK(*text == "xxx");
}

TEST_CASE("child_slot: knot slots are boxes with the same interface") {
    child_slot_t<PairFix> absent;
    CHECK(!absent);

    auto leaf = make_slot<PairFix>(PairFix{PairF<PairFix>{{}, {}}});
    REQUIRE(leaf);
    CHECK(!leaf->inner.left);
    CHECK(!leaf->inner.right);

    // One layer deep: an engaged left child, absent right.
    auto node = make_slot<PairFix>(PairFix{PairF<PairFix>{
        make_slot<PairFix>(PairFix{PairF<PairFix>{{}, {}}}),
        {},
    }});
    REQUIRE(node);
    CHECK(node->inner.left);
    CHECK(!node->inner.right);
}

TEST_CASE("child_slot: engaged test reads identically for both storages") {
    // The same generic text works whether the slot is Box or optional —
    // the interface contract the layer code relies on.
    auto engaged_count = [](const auto& slot) { return slot ? 1 : 0; };

    CHECK(engaged_count(make_slot<int>(1)) == 1);
    CHECK(engaged_count(child_slot_t<int>{}) == 0);
    CHECK(engaged_count(make_slot<PairFix>(PairFix{PairF<PairFix>{{}, {}}})) == 1);
    CHECK(engaged_count(child_slot_t<PairFix>{}) == 0);
}
