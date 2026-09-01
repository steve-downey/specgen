// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/tree_algorithms/config.hpp>
#include <catch2/catch_all.hpp>
#include <beman/tree_algorithms/todo.hpp>

TEST_CASE("todo", "[tree_algorithms::todo]") {
    const bool todo = true;
    CHECK(todo);
}
