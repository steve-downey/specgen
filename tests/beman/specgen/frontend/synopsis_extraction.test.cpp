// tests/beman/specgen/frontend/synopsis_extraction.test.cpp      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/synopsis_extraction.hpp>
#include <beman/specgen/frontend/synopsis_extraction.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

// Covered end to end by the build_document scenario tests in this
// directory; this file holds the header's own checks.
TEST_CASE("synopsis_extraction - the header compiles on its own") { SUCCEED(); }
