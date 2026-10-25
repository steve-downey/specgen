// tests/beman/specgen/frontend/source_files.test.cpp             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/source_files.hpp>
#include <beman/specgen/frontend/source_files.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

// Covered end to end by the build_document scenario tests in this
// directory; this file holds the header's own checks.
TEST_CASE("source_files - the header compiles on its own") { SUCCEED(); }
