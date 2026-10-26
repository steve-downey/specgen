// tests/beman/specgen/frontend/body_uses.test.cpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/body_uses.hpp>
#include <beman/specgen/frontend/body_uses.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

// Covered end to end by the build_document scenario tests in this
// directory; this file holds the header's own checks.
TEST_CASE("body_uses - the header compiles on its own") { SUCCEED(); }
