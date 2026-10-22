// tests/beman/specgen/foundation/enumeration.test.cpp              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/foundation/enumeration.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

namespace {

enum class Color { Red, Green, Blue };

} // namespace

template <>
inline constexpr auto beman::specgen::foundation::enumerators<Color> =
    std::array{Color::Red, Color::Green, Color::Blue};

namespace {

namespace foundation = beman::specgen::foundation;
using namespace std::string_view_literals;

constexpr auto names = foundation::table<Color>(foundation::is<Color::Green>("green"sv),
                                                foundation::is<Color::Red>("red"sv),
                                                foundation::is<Color::Blue>("blue"sv));

constexpr int channel(Color c) {
    return foundation::match(c,
                             foundation::when<Color::Red>([] { return 0; }),
                             foundation::when<Color::Green>([] { return 1; }),
                             foundation::when<Color::Blue>([] { return 2; }));
}

} // namespace

TEST_CASE("table - answers each enumerator, whatever order the cases were written in") {
    STATIC_CHECK(names[Color::Red] == "red");
    STATIC_CHECK(names[Color::Green] == "green");
    STATIC_CHECK(names[Color::Blue] == "blue");
}

TEST_CASE("match - runs the case for the enumerator") {
    STATIC_CHECK(channel(Color::Red) == 0);
    STATIC_CHECK(channel(Color::Green) == 1);
    STATIC_CHECK(channel(Color::Blue) == 2);
}

TEST_CASE("match - cases may return void and capture") {
    int seen = 0;
    foundation::match(Color::Blue,
                      foundation::when<Color::Red>([&] { seen = 1; }),
                      foundation::when<Color::Green>([&] { seen = 2; }),
                      foundation::when<Color::Blue>([&] { seen = 3; }));
    CHECK(seen == 3);
}
