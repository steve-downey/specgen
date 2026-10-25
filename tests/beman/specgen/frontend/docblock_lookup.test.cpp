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
