// tests/beman/specgen/frontend/declaration_masks.test.cpp        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/declaration_masks.hpp>
#include <beman/specgen/frontend/declaration_masks.hpp> // Re-inclusion verification

#include <beman/specgen/markers.hpp>

#include <catch2/catch_test_macros.hpp>

namespace frontend = beman::specgen::frontend;
using beman::specgen::grammar::Markers;

TEST_CASE("declaration_masks - an alias masks its right-hand side for \\impdef or a bare \\seebelow") {
    Markers markers;
    CHECK_FALSE(frontend::alias_mask(markers).has_value());
    markers.seebelow = true;
    CHECK(frontend::alias_mask(markers) == frontend::AliasMask::SeeBelow);
    markers.impdef = true;
    CHECK(frontend::alias_mask(markers) == frontend::AliasMask::ImplDefined);
    markers.impdef          = false;
    markers.seebelow_target = "noexcept";
    CHECK_FALSE(frontend::alias_mask(markers).has_value());
}

TEST_CASE("declaration_masks - a \\seebelow target names what it masks on a function") {
    Markers markers;
    CHECK_FALSE(frontend::seebelow_target(markers).has_value());
    markers.seebelow = true;
    CHECK(frontend::seebelow_target(markers) == frontend::SeeBelowTarget::ReturnType);
    markers.seebelow_target = "noexcept";
    CHECK(frontend::seebelow_target(markers) == frontend::SeeBelowTarget::Noexcept);
    markers.seebelow_target = "explicit";
    CHECK(frontend::seebelow_target(markers) == frontend::SeeBelowTarget::Explicit);
    markers.seebelow_target = "bogus";
    CHECK_FALSE(frontend::seebelow_target(markers).has_value());
}
