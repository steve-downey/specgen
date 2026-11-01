// tests/beman/specgen/frontend/foreign_names.test.cpp            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/foreign_names.hpp>
#include <beman/specgen/frontend/foreign_names.hpp> // Re-inclusion verification

#include <beman/specgen/frontend/build_document.hpp>
#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

namespace frontend = beman::specgen::frontend;
namespace ir       = beman::specgen::ir;

// Covered end to end by the build_document scenario tests in this
// directory; this file holds the header's own checks.
TEST_CASE("foreign_names - the header compiles on its own") { SUCCEED(); }

// A specified namespace is neither dropped nor foreign (issue #209): its
// qualifier survives the mapping because the draft writes it, and the leakage
// checker must not be handed it. The unmarked `detail` beside it still is.
TEST_CASE("foreign_names - a specified namespace's qualifier is not recorded as foreign") {
    const auto built = frontend::build_document(std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_nested_namespace.hpp");
    REQUIRE(built.has_value());
    const auto names = [&](const std::string& name) {
        return std::ranges::any_of(built->document.foreign_namespaces,
                                   [&](const ir::ForeignNamespace& ns) { return ns.name == name; });
    };
    CHECK(names("detail"));
    CHECK_FALSE(names("views"));
    CHECK_FALSE(names("proto"));
    CHECK_FALSE(names("ranges"));
}
