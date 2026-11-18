// tests/beman/specgen/backend/section_title.test.cpp              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/backend/section_title.hpp>
#include <beman/specgen/backend/section_title.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

namespace backend = beman::specgen::backend;

// Issue #351: a title that names code with a `&` in it, in each of its three
// spellings, is right in all three backends.
TEST_CASE("section_title - a backticked title is the draft's \\tcode, escaped as code") {
    CHECK(backend::render_section_title_draft("Partial specialization `thing<E&>`") ==
          "Partial specialization \\tcode{thing<E\\&>}");
    CHECK(backend::render_section_title("Partial specialization `thing<E&>`", '`') ==
          "Partial specialization `thing<E&>`");
    CHECK(backend::render_section_title("Partial specialization `thing<E&>`", '~') ==
          "Partial specialization ~thing<E&>~");
}

TEST_CASE("section_title - a bare character LaTeX reserves is escaped for the draft") {
    CHECK(backend::render_section_title_draft("Partial specialization thing<E&>, 50% #1") ==
          "Partial specialization thing<E\\&>, 50\\% \\#1");
    CHECK(backend::render_section_title("Partial specialization thing<E&>", '`') ==
          "Partial specialization thing<E&>");
}

TEST_CASE("section_title - the draft's own markup passes through, and is code elsewhere") {
    constexpr auto title = "Partial specialization \\tcode{thing<E\\&>}";
    CHECK(backend::render_section_title_draft(title) == title);
    CHECK(backend::render_section_title(title, '`') == "Partial specialization `thing<E&>`");
    CHECK(backend::render_section_title(title, '~') == "Partial specialization ~thing<E&>~");
    CHECK(backend::render_section_title_draft("Class template \\tcode{bad_expected_access<void>}") ==
          "Class template \\tcode{bad_expected_access<void>}");
    CHECK(backend::render_section_title("R\\&D \\ref{x}", '`') == "R&D \\ref{x}");
}
