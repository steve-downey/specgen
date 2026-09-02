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

// GCC 15 and 16 at -O3 -- and only there: -O2, every sanitizer configuration,
// and GCC 14 are all quiet -- peel this loop over a vector whose three elements
// they know, and then report sequence()'s push_back as reading the int payload
// of the element that holds the error. The has_value() check in front of it
// makes that read unreachable; what the optimizer lost is the correlation
// between an expected's discriminant and which union member is live. The
// diagnostic is attributed to the caller, not to traverse.hpp, so the caller is
// the only place it can be turned off -- a pragma around sequence() itself does
// not reach it.
#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
TEST_CASE("traverse - sequence returns the first error") {
    const std::vector<E> xs{E{1}, std::unexpected(std::string{"boom"}), E{3}};
    const auto           r = sequence(xs);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == "boom");
}
#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic pop
#endif

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
