// tests/beman/specgen/split_inline/rewrite.test.cpp      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/split_inline/rewrite.hpp>
#include <beman/specgen/split_inline/rewrite.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace split_inline = beman::specgen::split_inline;

// plan_rewrite is covered end to end by the cases in this directory
// (CMakeLists.txt); this file holds the Clang-free half.

TEST_CASE("apply_edits - no edits returns the source") { CHECK(split_inline::apply_edits("abc", {}) == "abc"); }

TEST_CASE("apply_edits - a replacement, an insertion and a removal") {
    const std::vector<split_inline::Edit> edits{
        {0, 1, "X"}, // replace `a`
        {2, 2, "+"}, // insert before `c`
        {3, 4, ""},  // remove `d`
    };
    CHECK(split_inline::apply_edits("abcd", edits) == "Xb+c");
}

TEST_CASE("apply_edits_tracked - insertions are reported where they landed, replacements are not") {
    const std::vector<split_inline::Edit> edits{
        {0, 1, "XY"}, // replace `a` with two bytes, shifting everything after
        {2, 2, "++"}, // insert before `c`
        {3, 3, "-"},  // insert before `d`
    };
    const split_inline::Applied applied = split_inline::apply_edits_tracked("abcd", edits);
    CHECK(applied.text == "XYb++c-d");
    CHECK(applied.inserted == std::vector<split_inline::Span>{{3, 5}, {6, 7}});
}

TEST_CASE("format_inserted - nothing inserted, nothing changes") {
    CHECK(split_inline::format_inserted("/nowhere/header.hpp", "int   x ;", {}) == "int   x ;");
}

TEST_CASE("format_inserted - no .clang-format above the header keeps the bytes") {
    CHECK(split_inline::format_inserted("/nowhere/header.hpp", "int   x ;", {{0, 9}}) == "int   x ;");
}

TEST_CASE("apply_edits - insertions at one offset apply in list order") {
    const std::vector<split_inline::Edit> edits{{1, 1, "1"}, {1, 1, "2"}, {1, 1, "3"}};
    CHECK(split_inline::apply_edits("ab", edits) == "a123b");
}

TEST_CASE("apply_edits - an overlapping edit is ignored rather than applied twice") {
    const std::vector<split_inline::Edit> edits{{0, 2, "X"}, {1, 3, "Y"}};
    CHECK(split_inline::apply_edits("abcd", edits) == "Xcd");
}
