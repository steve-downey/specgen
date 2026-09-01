// tests/beman/specgen/foundation/overloaded.test.cpp            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/foundation/overloaded.hpp>
#include <beman/specgen/foundation/overloaded.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>

using beman::specgen::foundation::overloaded;

namespace {

using V = std::variant<int, double, std::string>;

std::string classify(const V& v) {
    return std::visit(overloaded{
                          [](int) { return std::string{"int"}; },
                          [](double) { return std::string{"double"}; },
                          [](const std::string&) { return std::string{"string"}; },
                      },
                      v);
}

} // namespace

TEST_CASE("overloaded - dispatches to the matching case") {
    CHECK(classify(V{42}) == "int");
    CHECK(classify(V{3.5}) == "double");
    CHECK(classify(V{std::string{"hi"}}) == "string");
}

TEST_CASE("overloaded - state can be carried in a case") {
    // Every alternative is handled explicitly — the tripwire deliberately makes
    // a generic [](auto&&) catch-all ambiguous with its consteval fallback, so
    // "handle one, ignore the rest with auto" is not an option (that is the
    // point of decision visitation-rules).
    int  seen = 0;
    auto v    = overloaded{
        [&](int) { ++seen; },
        [&](double) {},
        [&](const std::string&) {},
    };
    std::visit(v, V{1});
    std::visit(v, V{2.0});
    CHECK(seen == 1);
}

// The exhaustiveness tripwire is a compile-time guarantee: omitting a case makes
// std::visit select the consteval catch-all, whose static_assert(false) is a
// hard compile error naming the omission (decision visitation-rules). It therefore cannot be
// exercised as a passing runtime test — a variant with an unhandled alternative
// would fail to compile. This test documents that the handled-everything path
// builds and runs; the negative path is covered by construction.
TEST_CASE("overloaded - a fully-handled visit compiles and runs") { CHECK(classify(V{0}) == "int"); }
