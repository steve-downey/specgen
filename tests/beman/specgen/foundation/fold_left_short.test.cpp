// tests/beman/specgen/foundation/fold_left_short.test.cpp       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/foundation/fold_left_short.hpp>
#include <beman/specgen/foundation/fold_left_short.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <string>
#include <vector>

using beman::specgen::foundation::fold_left_short;

namespace {

// Step function: accumulate, but fail on a negative element. The error carries
// the offending value so we can assert which element stopped the fold.
std::expected<int, std::string> add_nonneg(int acc, int x) {
    if (x < 0) {
        return std::unexpected("negative: " + std::to_string(x));
    }
    return acc + x;
}

} // namespace

TEST_CASE("fold_left_short - sums when every step succeeds") {
    const std::vector<int> xs{1, 2, 3, 4};
    const auto             r = fold_left_short(xs, 0, add_nonneg);
    REQUIRE(r.has_value());
    CHECK(r.value() == 10);
}

TEST_CASE("fold_left_short - stops at the first failing step and does not visit later elements") {
    int                    visited = 0;
    const std::vector<int> xs{1, 2, -7, 100};
    const auto             r = fold_left_short(xs, 0, [&](int acc, int x) -> std::expected<int, std::string> {
        ++visited;
        if (x < 0) {
            return std::unexpected(std::string{"stop"});
        }
        return acc + x;
    });
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == "stop");
    // 1, 2, then -7 fails; 100 is never seen — the early exit that distinguishes
    // this from std::ranges::fold_left and from traverse.
    CHECK(visited == 3);
}

TEST_CASE("fold_left_short - empty range yields the initial accumulator") {
    const std::vector<int> xs{};
    const auto             r = fold_left_short(xs, 99, add_nonneg);
    REQUIRE(r.has_value());
    CHECK(r.value() == 99);
}
