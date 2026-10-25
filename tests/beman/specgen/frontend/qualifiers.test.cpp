// tests/beman/specgen/frontend/qualifiers.test.cpp               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/qualifiers.hpp>
#include <beman/specgen/frontend/qualifiers.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

// Every corpus header that writes `std::` or the header's own namespace, or
// marks a `detail::` helper `\expos`, exercises this through the
// build_document scenario tests (expos_uses.test.cpp, synopsis.test.cpp).
TEST_CASE("qualifiers - the header compiles on its own") { SUCCEED(); }
