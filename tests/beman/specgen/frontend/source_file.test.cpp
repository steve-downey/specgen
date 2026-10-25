// tests/beman/specgen/frontend/source_file.test.cpp              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/source_file.hpp>
#include <beman/specgen/frontend/source_file.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

// Covered end to end by the build_document scenario tests in this
// directory; this file holds the header's own checks.
TEST_CASE("source_file - the header compiles on its own") { SUCCEED(); }
