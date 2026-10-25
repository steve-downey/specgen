// tests/beman/specgen/frontend/file_views.test.cpp               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/file_views.hpp>
#include <beman/specgen/frontend/file_views.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

// Exercised through every build_document scenario test; a direct case
// needs a multi-file document, which the corpus's issue-77 headers supply
// to those tests already.
TEST_CASE("file_views - the header compiles on its own") { SUCCEED(); }
