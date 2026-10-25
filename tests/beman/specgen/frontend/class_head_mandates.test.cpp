// tests/beman/specgen/frontend/class_head_mandates.test.cpp      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/class_head_mandates.hpp>
#include <beman/specgen/frontend/class_head_mandates.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

// Covered end to end by the build_document scenario tests in this
// directory; this file holds the header's own checks.
TEST_CASE("class_head_mandates - the header compiles on its own") { SUCCEED(); }
