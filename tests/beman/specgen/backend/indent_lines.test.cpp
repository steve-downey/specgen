// tests/beman/specgen/backend/indent_lines.test.cpp               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/backend/indent_lines.hpp>
#include <beman/specgen/backend/indent_lines.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

namespace backend = beman::specgen::backend;

TEST_CASE("indent_lines - every line is indented, and an empty one stays empty") {
    CHECK(backend::indent_lines("", "  ").empty());
    CHECK(backend::indent_lines("one", "  ") == "  one");
    CHECK(backend::indent_lines("one\ntwo\n", "  ") == "  one\n  two\n");
    CHECK(backend::indent_lines("```cpp\nx;\n```\n\nafter\n", "  ") == "  ```cpp\n  x;\n  ```\n\n  after\n");
}
