// tests/beman/specgen/backend/element_label.test.cpp              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/backend/element_label.hpp>
#include <beman/specgen/backend/element_label.hpp> // Re-inclusion verification

#include <beman/specgen/foundation/enumeration.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace beman::specgen::ir;
namespace backend = beman::specgen::backend;

// The labels are [structure.specifications]'s, not ir::element_name's macro
// spellings: several differ, and those are the ones a hand-written table gets
// wrong.
TEST_CASE("element_label - element_label spells the labels that differ from the macro name") {
    CHECK(backend::element_label(ElementKind::Expects) == "Preconditions");
    CHECK(backend::element_label(ElementKind::HardExpects) == "Hardened preconditions");
    CHECK(backend::element_label(ElementKind::Ensures) == "Postconditions");
    CHECK(backend::element_label(ElementKind::Sync) == "Synchronization");
    CHECK(backend::element_label(ElementKind::Errors) == "Error conditions");
}

TEST_CASE("element_label - every element kind has a label") {
    for (const auto& [kind, label] : backend::kElementLabels) {
        CAPTURE(label);
        CHECK(backend::element_label(kind) == label);
        CHECK_FALSE(label.empty());
    }
    CHECK(backend::kElementLabels.size() == beman::specgen::foundation::enumerators<ElementKind>.size());
}
