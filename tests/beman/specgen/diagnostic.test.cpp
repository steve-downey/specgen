// tests/beman/specgen/diagnostic.test.cpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/diagnostic.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using beman::specgen::Severity;
using beman::specgen::severity_label;

TEST_CASE("severity_label - spells each severity as its printers write it") {
    STATIC_CHECK(std::string_view{severity_label(Severity::Error)} == "error");
    STATIC_CHECK(std::string_view{severity_label(Severity::Warning)} == "warning");
    STATIC_CHECK(std::string_view{severity_label(Severity::Note)} == "note");
}
