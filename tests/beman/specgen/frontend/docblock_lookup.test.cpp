// tests/beman/specgen/frontend/docblock_lookup.test.cpp          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/docblock_lookup.hpp>
#include <beman/specgen/frontend/docblock_lookup.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

namespace frontend = beman::specgen::frontend;
using frontend::CommentVocabulary;

TEST_CASE("docblock_lookup - a line's vocabulary is markup, Doxygen, or draft form") {
    CHECK(frontend::line_vocabulary("//! \\effects") == CommentVocabulary::Markup);
    CHECK(frontend::line_vocabulary("/*! \\effects */") == CommentVocabulary::Markup);
    CHECK(frontend::line_vocabulary("/// Does a thing.") == CommentVocabulary::Doxygen);
    CHECK(frontend::line_vocabulary("/** Does a thing. */") == CommentVocabulary::Doxygen);
    CHECK(frontend::line_vocabulary("// \\ref{optional.dtor}") == CommentVocabulary::Draft);
}

TEST_CASE("docblock_lookup - markup partway into a merged comment starts at its own line") {
    const char* raw = "// \\ref{optional.dtor}, destructor\n  //! \\merge";
    REQUIRE(frontend::docblock_start(raw).has_value());
    CHECK(*frontend::docblock_start(raw) == std::string_view(raw).find('\n') + 1);
    CHECK(frontend::is_docblock_comment(raw));
}

TEST_CASE("docblock_lookup - Doxygen is stripped from a synopsis but is not a docblock") {
    const char* raw = "// \\ref{optional.dtor}\n/// Destroys the value.";
    CHECK_FALSE(frontend::docblock_start(raw).has_value());
    CHECK_FALSE(frontend::is_docblock_comment(raw));
    REQUIRE(frontend::stripped_comment_start(raw).has_value());
    CHECK(*frontend::stripped_comment_start(raw) == std::string_view(raw).find('\n') + 1);
}

TEST_CASE("docblock_lookup - \\omit, \\merge and \\elsewhere suppress the generated entity") {
    beman::specgen::lowering::ItemDirectives directives;
    CHECK_FALSE(frontend::suppresses_generated_entity(directives));
    directives.merge = true;
    CHECK(frontend::suppresses_generated_entity(directives));
}

TEST_CASE("docblock_lookup - a synopsis comment is marked by a leading \\syn") {
    // The range is the marker and the whitespace after it: what a synopsis
    // leaves out of the comment it keeps (issue #280).
    const auto line = frontend::synopsis_comment_marker("// \\syn access functions");
    REQUIRE(line.has_value());
    CHECK(line->first == 3);
    CHECK(line->second == 8);

    const auto block = frontend::synopsis_comment_marker("/* \\syn the count */");
    REQUIRE(block.has_value());
    CHECK(block->first == 3);

    // The marker alone, and with no space after the comment introducer.
    CHECK(frontend::synopsis_comment_marker("//\\syn").has_value());

    // Not the marker: another command, a longer word, the marker later in
    // the text, and anything that is not a plain comment's start.
    CHECK_FALSE(frontend::synopsis_comment_marker("// \\ref{optional.ctor}, constructors").has_value());
    CHECK_FALSE(frontend::synopsis_comment_marker("// \\synopsis of the class").has_value());
    CHECK_FALSE(frontend::synopsis_comment_marker("// see \\syn").has_value());
    CHECK_FALSE(frontend::synopsis_comment_marker("int x; // \\syn").has_value());
}
