// tests/beman/specgen/synopsis_edits.test.cpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/synopsis_edits.hpp>
#include <beman/specgen/synopsis_edits.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace beman::specgen::synopsis_edits;

TEST_CASE("synopsis_edits - back to front is descending begin, the longer first on a tie") {
    std::vector<SynopsisEdit> edits = {{2, 4, "a"}, {9, 10, "b"}, {2, 8, "c"}, {5, 5, "d"}};
    sort_back_to_front(edits);
    REQUIRE(edits.size() == 4);
    CHECK(edits[0].replacement == "b");
    CHECK(edits[1].replacement == "d");
    CHECK(edits[2].replacement == "c");
    CHECK(edits[3].replacement == "a");
}

TEST_CASE("synopsis_edits - edits equal in both offsets keep their order") {
    std::vector<SynopsisEdit> edits = {{3, 3, "first"}, {3, 3, "second"}};
    sort_back_to_front(edits);
    CHECK(edits[0].replacement == "first");
    CHECK(edits[1].replacement == "second");
}

TEST_CASE("synopsis_edits - overlap is a shared interior, not a shared endpoint") {
    CHECK(edits_overlap({0, 5, ""}, {4, 9, ""}));
    CHECK(edits_overlap({0, 9, ""}, {3, 4, ""}));
    CHECK_FALSE(edits_overlap({0, 5, ""}, {5, 9, ""}));
}

TEST_CASE("synopsis_edits - a dominant edit replaces every edit it overlaps") {
    std::vector<SynopsisEdit> edits = {{0, 2, "kept"}, {4, 6, "inner"}, {7, 12, "straddles"}};
    add_dominant_edit(edits, {3, 8, "see below"});
    REQUIRE(edits.size() == 2);
    CHECK(edits[0].replacement == "kept");
    CHECK(edits[1].replacement == "see below");
}
