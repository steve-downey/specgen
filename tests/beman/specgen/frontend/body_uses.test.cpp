// tests/beman/specgen/frontend/body_uses.test.cpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/body_uses.hpp>
#include <beman/specgen/frontend/body_uses.hpp> // Re-inclusion verification

#include <beman/specgen/frontend/build_document.hpp>
#include <beman/specgen/ir.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <string>

// Covered end to end by the build_document scenario tests in this
// directory; this file holds the header's own checks.
TEST_CASE("body_uses - the header compiles on its own") { SUCCEED(); }

TEST_CASE("body_uses - authored examples do not hide uses in an unextracted implementation") {
    const auto built = beman::specgen::frontend::build_document(std::string(BEMAN_SPECGEN_CORPUS_DIR) +
                                                                "/spec_authored_code_body_uses.hpp");
    REQUIRE(built.has_value());
    CHECK(built->diagnostics.empty());
    CHECK(std::ranges::any_of(built->document.unextracted_uses, [](const beman::specgen::ir::BodyUse& use) {
        return use.function == "wrapper::run" && use.member == "hidden_";
    }));
}
