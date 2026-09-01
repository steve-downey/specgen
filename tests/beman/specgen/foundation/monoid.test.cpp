// tests/beman/specgen/foundation/monoid.test.cpp               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/foundation/monoid.hpp>
#include <beman/specgen/foundation/monoid.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using beman::specgen::foundation::mconcat;
using beman::specgen::foundation::mconcat_map;
using beman::specgen::foundation::monoid;

namespace {

// Two representative instances: (int, +, 0) and (string, concat, "").
constexpr auto sum    = monoid{[](int a, int b) { return a + b; }, 0};
const auto     concat = monoid{[](std::string a, std::string b) { return a + b; }, std::string{}};

} // namespace

TEST_CASE("monoid - identity law: combine(identity, a) == a == combine(a, identity)") {
    CHECK(sum.combine(sum.identity, 7) == 7);
    CHECK(sum.combine(7, sum.identity) == 7);
    CHECK(concat.combine(concat.identity, "x") == "x");
    CHECK(concat.combine("x", concat.identity) == "x");
}

TEST_CASE("monoid - associativity law: combine(a, combine(b, c)) == combine(combine(a, b), c)") {
    CHECK(sum.combine(1, sum.combine(2, 3)) == sum.combine(sum.combine(1, 2), 3));
    CHECK(concat.combine("a", concat.combine("b", "c")) == concat.combine(concat.combine("a", "b"), "c"));
}

TEST_CASE("monoid - mconcat folds a range from the identity") {
    const std::vector<int> xs{1, 2, 3, 4};
    CHECK(mconcat(xs, sum) == 10);

    const std::vector<std::string> words{"a", "b", "c"};
    CHECK(mconcat(words, concat) == "abc");
}

TEST_CASE("monoid - mconcat of an empty range is the identity") {
    CHECK(mconcat(std::vector<int>{}, sum) == 0);
    CHECK(mconcat(std::vector<std::string>{}, concat) == "");
}

TEST_CASE("monoid - mconcat_map maps then combines (the fold-map shape)") {
    const std::vector<std::string> words{"aa", "b", "ccc"};
    // map each string to its length, sum the lengths.
    auto length = [](const std::string& s) { return static_cast<int>(s.size()); };
    CHECK(mconcat_map(words, length, sum) == 6);
}
