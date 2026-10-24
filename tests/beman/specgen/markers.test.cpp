// tests/beman/specgen/markers.test.cpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/markers.hpp>
#include <beman/specgen/markers.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string_view>

using namespace beman::specgen::grammar;

// The registry's own invariants. Whether parse_docblock honors each row is
// docblock.test.cpp's business; these cases check the table and its lookup.

TEST_CASE("markers - find_marker returns each registry row for its spelling") {
    for (const MarkerInfo& entry : kMarkers) {
        CAPTURE(entry.spelling);
        CHECK(find_marker(entry.spelling) == &entry);
    }
}

TEST_CASE("markers - find_marker rejects a spelling that names no marker") {
    CHECK(find_marker("effects") == nullptr); // an element tag, not a marker
    CHECK(find_marker("\\merge") == nullptr); // the spelling excludes the backslash
    CHECK(find_marker("") == nullptr);
}

TEST_CASE("markers - every spelling appears once") {
    std::set<std::string_view> seen;
    for (const MarkerInfo& entry : kMarkers) {
        CAPTURE(entry.spelling);
        CHECK(seen.insert(entry.spelling).second);
    }
}

TEST_CASE("markers - each row's arity agrees with what it can store") {
    for (const MarkerInfo& entry : kMarkers) {
        CAPTURE(entry.spelling);
        // Only a marker with a setter has anywhere to put argument text.
        CHECK((entry.arity == MarkerArity::Flag) == (entry.set_arg == nullptr));
        // A required argument has a message for when it is missing, and
        // nothing else does.
        CHECK((entry.arity == MarkerArity::RestRequired) == !entry.missing_arg_error.empty());
    }
}

TEST_CASE("markers - a setter reports whether it replaced a value") {
    for (const MarkerInfo& entry : kMarkers) {
        if (entry.set_arg == nullptr)
            continue;
        CAPTURE(entry.spelling);
        Markers markers;
        CHECK_FALSE(entry.set_arg(markers, "first"));
        CHECK(entry.set_arg(markers, "second"));
    }
}

TEST_CASE("markers - the setters store their argument text") {
    Markers markers;
    detail::set_expos_name(markers, "name");
    detail::set_at_anchor(markers, "anchor");
    detail::set_seebelow_target(markers, "noexcept");
    detail::set_group_id(markers, "group");
    detail::set_also_target(markers, "target");
    CHECK(markers.expos_name == "name");
    CHECK(markers.at_anchor == "anchor");
    CHECK(markers.seebelow_target == "noexcept");
    CHECK(markers.group_id == "group");
    CHECK(markers.also_target == "target");
}
