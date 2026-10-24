// tests/beman/specgen/foundation/enumeration_missing.probe.cpp     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Must fail to compile: the table and the match each leave `Blue` out, and
// the diagnostic names it. Built by the foundation.enumeration.missing-case
// test, which expects that failure.

#include <beman/specgen/foundation/enumeration.hpp>

#include <array>

enum class Color { Red, Green, Blue };

template <>
inline constexpr auto beman::specgen::foundation::enumerators<Color> =
    std::array{Color::Red, Color::Green, Color::Blue};

namespace foundation = beman::specgen::foundation;

constexpr auto incomplete = foundation::table<Color>(foundation::is<Color::Red>(1), foundation::is<Color::Green>(2));

int main() {}
