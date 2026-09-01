// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/tree_algorithms/overloaded.hpp>
#include <beman/tree_algorithms/overloaded.hpp> // Re-inclusion: verifies include guard

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>

using beman::tree_algorithms::overloaded;

TEST_CASE("overloaded - HeaderIsIdempotent", "[tree_algorithms::overloaded]") { REQUIRE(true); }

TEST_CASE("overloaded - BasicDispatch", "[tree_algorithms::overloaded]") {
    std::variant<int, std::string> v = 42;

    int result = std::visit(overloaded{
                                [](int x) { return x; },
                                [](const std::string&) { return -1; },
                            },
                            v);

    CHECK(result == 42);
}

TEST_CASE("overloaded - DispatchesOnActiveAlternative", "[tree_algorithms::overloaded]") {
    std::variant<int, double, std::string> v;

    v        = 3.14;
    double d = std::visit(overloaded{
                              [](int) -> double { return 0.0; },
                              [](double x) -> double { return x; },
                              [](const std::string&) -> double { return -1.0; },
                          },
                          v);
    CHECK(d == 3.14);

    v        = std::string{"hello"};
    double s = std::visit(overloaded{
                              [](int) -> double { return 0.0; },
                              [](double) -> double { return 0.0; },
                              [](const std::string&) -> double { return 99.0; },
                          },
                          v);
    CHECK(s == 99.0);
}

TEST_CASE("overloaded - CTAD deduces from lambdas", "[tree_algorithms::overloaded]") {
    // Verify that overloaded{f, g} deduces without an explicit deduction guide.
    auto vis = overloaded{
        [](int x) { return x * 2; },
        [](double x) { return static_cast<int>(x); },
    };

    std::variant<int, double> v = 7;
    CHECK(std::visit(vis, v) == 14);
}
