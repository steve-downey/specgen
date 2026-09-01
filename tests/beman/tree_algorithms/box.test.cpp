// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/tree_algorithms/box.hpp>
#include <beman/tree_algorithms/box.hpp> // Re-inclusion: verifies include guard

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <type_traits>

using beman::tree_algorithms::Box;
using beman::tree_algorithms::make_box;

static_assert(std::is_default_constructible_v<Box<int>>);

namespace {

// Box is aggregate-friendly: a struct holding a Box stays an aggregate and
// can be aggregate-initialised (nullable default, no explicit default ctor).
struct BoxAggregate {
    Box<int> boxed;
    int      tag = 0;
};

static_assert(std::is_aggregate_v<BoxAggregate>);
static_assert(BoxAggregate{}.tag == 0);
static_assert(BoxAggregate{}.boxed.ptr == nullptr);
static_assert(BoxAggregate{Box<int>{}, 3}.tag == 3);

constexpr auto box_deep_copy_roundtrip() -> bool {
    auto     b1 = make_box<int>(7);
    Box<int> b2 = b1;
    bool     ok = (*b1 == *b2) && (b1 == b2);
    *b2         = 99;
    ok          = ok && (*b1 == 7) && (*b2 == 99) && !(b1 == b2);
    return ok;
}

static_assert(box_deep_copy_roundtrip());

} // namespace

TEST_CASE("Box - MakeBoxInt", "[tree_algorithms::box]") {
    auto b = make_box<int>(42);
    CHECK(*b == 42);
}

TEST_CASE("Box - MakeBoxString", "[tree_algorithms::box]") {
    auto b = make_box<std::string>("hello");
    CHECK(*b == "hello");
}

TEST_CASE("Box - DeepCopyOnCopy", "[tree_algorithms::box]") {
    auto     b1 = make_box<int>(7);
    Box<int> b2 = b1;
    CHECK(*b1 == *b2);
    *b2 = 99;
    CHECK(*b1 == 7);
    CHECK(*b2 == 99);
}
