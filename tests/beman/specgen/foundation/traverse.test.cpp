// tests/beman/specgen/foundation/traverse.test.cpp             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/foundation/traverse.hpp>
#include <beman/specgen/foundation/traverse.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <string>
#include <vector>

using beman::specgen::foundation::sequence;
using beman::specgen::foundation::traverse;

namespace {

using E = std::expected<int, std::string>;

std::expected<int, std::string> parse_positive(int x) {
    if (x <= 0) {
        return std::unexpected("non-positive: " + std::to_string(x));
    }
    return x * 10;
}

} // namespace

TEST_CASE("traverse - sequence collects values when all succeed") {
    const std::vector<E> xs{E{1}, E{2}, E{3}};
    const auto           r = sequence(xs);
    REQUIRE(r.has_value());
    CHECK(r.value() == std::vector<int>{1, 2, 3});
}

TEST_CASE("traverse - sequence returns the first error") {
    const std::vector<E> xs{E{1}, std::unexpected(std::string{"boom"}), E{3}};
    const auto           r = sequence(xs);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == "boom");
}

TEST_CASE("traverse - maps and collects when every element succeeds") {
    const std::vector<int> xs{1, 2, 3};
    const auto             r = traverse(xs, parse_positive);
    REQUIRE(r.has_value());
    CHECK(r.value() == std::vector<int>{10, 20, 30});
}

TEST_CASE("traverse - fails fast on the first bad element") {
    const std::vector<int> xs{1, 0, 3};
    const auto             r = traverse(xs, parse_positive);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == "non-positive: 0");
}

TEST_CASE("traverse - empty input yields an empty vector") {
    const auto r = traverse(std::vector<int>{}, parse_positive);
    REQUIRE(r.has_value());
    CHECK(r.value().empty());
}
