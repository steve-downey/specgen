// tests/beman/specgen/frontend/inclass_items.test.cpp            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/inclass_items.hpp>
#include <beman/specgen/frontend/inclass_items.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

// Covered end to end by the build_document scenario tests in this
// directory; this file holds the header's own checks.
TEST_CASE("inclass_items - the header compiles on its own") { SUCCEED(); }
