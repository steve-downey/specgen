// tests/beman/specgen/frontend/decl_projections.test.cpp         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/decl_projections.hpp>
#include <beman/specgen/frontend/decl_projections.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

// Exercised through the build_document scenario tests, which reach every
// projection on corpus declarations.
TEST_CASE("decl_projections - the header compiles on its own") { SUCCEED(); }
