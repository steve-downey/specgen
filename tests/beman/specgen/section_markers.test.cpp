// tests/beman/specgen/section_markers.test.cpp                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/section_markers.hpp>
#include <beman/specgen/section_markers.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace beman::specgen::grammar;

TEST_CASE("section_markers - an END fence names the section it closes") {
    const auto fence = section_end_fence("/// END [optional.syn]");
    REQUIRE(fence.has_value());
    CHECK(fence->stable == "optional.syn");
    CHECK(fence->line_begin == 0);
}

TEST_CASE("section_markers - an END fence merged into a longer comment reports its own line") {
    const std::string raw   = "//! \\verbatim-synopsis\n/// END [optional.syn]";
    const auto        fence = section_end_fence(raw);
    REQUIRE(fence.has_value());
    CHECK(fence->stable == "optional.syn");
    CHECK(fence->line_begin == raw.find("///"));
}

TEST_CASE("section_markers - only a Doxygen line with a non-empty name is a fence") {
    CHECK_FALSE(section_end_fence("// END [optional.syn]").has_value());
    CHECK_FALSE(section_end_fence("/// END []").has_value());
    CHECK_FALSE(section_end_fence("/// END optional.syn").has_value());
}

TEST_CASE("section_markers - a ref comment yields its stable name and drops the label") {
    CHECK(parse_ref("// \\ref{optional.observe}, observers") == "optional.observe");
    CHECK(parse_ref("  //! \\ref{optional.assign}") == "optional.assign");
}

TEST_CASE("section_markers - anything else is not a ref comment") {
    CHECK_FALSE(parse_ref("// observers").has_value());
    CHECK_FALSE(parse_ref("/* \\ref{optional.observe} */").has_value());
    CHECK_FALSE(parse_ref("// \\ref{optional.observe").has_value());
}
