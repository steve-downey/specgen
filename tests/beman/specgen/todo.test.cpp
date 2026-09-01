// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/config.hpp>
#include <catch2/catch_all.hpp>
#include <beman/specgen/todo.hpp>

TEST_CASE("todo", "[specgen::todo]") {
    const bool todo = true;
    CHECK(todo);
}
