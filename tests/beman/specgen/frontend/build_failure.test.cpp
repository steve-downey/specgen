// tests/beman/specgen/frontend/build_failure.test.cpp            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/build_failure.hpp>
#include <beman/specgen/frontend/build_failure.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

TEST_CASE("build_failure - the header compiles on its own") { SUCCEED(); }
