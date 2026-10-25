// tests/beman/specgen/frontend/crash_handler.test.cpp            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/crash_handler.hpp>
#include <beman/specgen/frontend/crash_handler.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

namespace frontend = beman::specgen::frontend;

// What the handler does happens on a fatal signal, which a test process
// cannot take and still report, so this checks only that installing the
// handler links and returns.
TEST_CASE("crash_handler - installing the handler returns") {
    frontend::install_crash_handler("crash_handler.test", "banner\n");
    SUCCEED();
}
