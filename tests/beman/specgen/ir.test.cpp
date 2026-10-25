// tests/beman/specgen/ir.test.cpp                                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/ir.hpp>
#include <beman/specgen/ir.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

using namespace beman::specgen::ir;

TEST_CASE("ir - HeaderIsIdempotent") {
    // Placeholder: verifies header re-inclusion safety and build coherency.
    // This test always passes if the file compiles.
    REQUIRE(true);
}

TEST_CASE("ir - element names round-trip") {
    for (int i = 0; i <= static_cast<int>(ElementKind::Errors); ++i) {
        auto kind = static_cast<ElementKind>(i);
        auto name = element_name(kind);
        CHECK(name != "?");
        auto back = element_from_name(name);
        CHECK(back.has_value());
        CHECK(*back == kind);
    }
    CHECK(!element_from_name("bogus").has_value());
}

TEST_CASE("ir - disposition names round-trip") {
    for (int i = 0; i <= static_cast<int>(Disposition::Undocumented); ++i) {
        auto disposition = static_cast<Disposition>(i);
        auto name        = disposition_name(disposition);
        CHECK(name != "?");
        auto back = disposition_from_name(name);
        CHECK(back.has_value());
        CHECK(*back == disposition);
    }
    CHECK(!disposition_from_name("bogus").has_value());
}

TEST_CASE("ir - canonicalize sorts into structure.specifications order") {
    ItemDescr d;
    d.elements.push_back({ElementKind::Returns, {}, {}});
    d.elements.push_back({ElementKind::Mandates, {}, {}});
    d.elements.push_back({ElementKind::Effects, {}, {}});
    canonicalize(d);
    REQUIRE(d.elements.size() == 3);
    CHECK(d.elements[0].kind == ElementKind::Mandates);
    CHECK(d.elements[1].kind == ElementKind::Effects);
    CHECK(d.elements[2].kind == ElementKind::Returns);
}

TEST_CASE("ir - canonicalize sorts a derived element before an authored one of the same kind") {
    // Design §5.2: "derived conjuncts first, authored prose
    // appended". The authored element is pushed first here (the order
    // frontend.cpp builds them in -- the docblock is lowered before the
    // derivation runs), so this also confirms canonicalize actually
    // reorders rather than merely leaving a pre-sorted input alone.
    DescriptionElement authored;
    authored.kind = ElementKind::Mandates;

    DescriptionElement derived;
    derived.kind    = ElementKind::Mandates;
    derived.derived = true;

    ItemDescr d;
    d.elements.push_back(authored);
    d.elements.push_back(derived);
    canonicalize(d);

    REQUIRE(d.elements.size() == 2);
    CHECK(d.elements[0].derived);
    CHECK_FALSE(d.elements[1].derived);
}

TEST_CASE("ir - member kind names round-trip") {
    for (const MemberKind kind : {MemberKind::Function, MemberKind::Data, MemberKind::Alias}) {
        const auto name = member_kind_name(kind);
        CHECK(name != "?");
        const auto back = member_kind_from_name(name);
        REQUIRE(back.has_value());
        CHECK(*back == kind);
    }
    CHECK(!member_kind_from_name("bogus").has_value());
}
