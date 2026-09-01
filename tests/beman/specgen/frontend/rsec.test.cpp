// tests/beman/specgen/frontend/rsec.test.cpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// parse_rsec() is built on the combinator library (decision
// parser-combinators). Positive cases pin the exact grammar it accepts
// (decoration variants, the no-whitespace-between-tag-and-depth rule, empty
// bracket/brace content) so a combinator refactor stays byte-identical for
// every well-formed \rSec. Negative cases assert not just that a malformed
// \rSec fails but *where* -- the point of positioned parse failures -- and
// cover the edge that is one bad conversion away from a crash: an
// out-of-range depth is a positioned failure like any other malformed
// \rSec, never a throw.
//
// One naming constraint, since the cases here are all about brackets: a
// TEST_CASE name must not contain an *unbalanced* '[' or ']'. Catch2's
// CatchAddTests.cmake through 3.7.1 splits its `--list-tests` output with an
// unquoted `foreach(line ${output})`, and CMake's list expansion does not
// break a ';' that sits inside an unclosed '['. An unbalanced bracket in a
// name therefore swallows the following test names into one bogus ctest
// entry, which then matches nothing and fails. Spell the character out.

#include <beman/specgen/frontend/frontend.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace frontend = beman::specgen::frontend;

// --- well-formed markers: the exact accepted grammar ------------------------

TEST_CASE("parse_rsec - well-formed marker with plain // decoration") {
    auto r = frontend::parse_rsec("// \\rSec2[algorithms.sort]{Sorting}");
    REQUIRE(r.has_value());
    CHECK(r->value.depth == 2);
    CHECK(r->value.stable == "algorithms.sort");
    CHECK(r->value.title == "Sorting");
}

TEST_CASE("parse_rsec - well-formed marker with /// decoration") {
    auto r = frontend::parse_rsec("/// \\rSec1[general]{General utilities}");
    REQUIRE(r.has_value());
    CHECK(r->value.depth == 1);
    CHECK(r->value.stable == "general");
    CHECK(r->value.title == "General utilities");
}

TEST_CASE("parse_rsec - well-formed marker with //! decoration and tab whitespace") {
    auto r = frontend::parse_rsec("//!\t\\rSec3\t[x.y]\t{Z}");
    REQUIRE(r.has_value());
    CHECK(r->value.depth == 3);
    CHECK(r->value.stable == "x.y");
    CHECK(r->value.title == "Z");
}

TEST_CASE("parse_rsec - empty stable name and title are accepted") {
    // Non-empty bracket/brace content is not required.
    auto r = frontend::parse_rsec("// \\rSec1[]{}");
    REQUIRE(r.has_value());
    CHECK(r->value.depth == 1);
    CHECK(r->value.stable.empty());
    CHECK(r->value.title.empty());
}

TEST_CASE("parse_rsec - leading blanks before the decoration are tolerated") {
    auto r = frontend::parse_rsec("   // \\rSec4[a]{B}");
    REQUIRE(r.has_value());
    CHECK(r->value.depth == 4);
}

// --- non-matches: "not a structure comment", not an error ------------------

TEST_CASE("parse_rsec - a \\ref group header is a non-match") {
    auto r = frontend::parse_rsec("// \\ref{foo.bar}, Human Label");
    CHECK(!r.has_value());
}

TEST_CASE("parse_rsec - input without comment decoration fails at the start") {
    auto r = frontend::parse_rsec("\\rSec2[foo]{Title}");
    REQUIRE(!r.has_value());
    CHECK(r.error().where.offset == 0);
}

TEST_CASE("parse_rsec - whitespace between the tag and the depth is not tolerated") {
    // skip_ws() runs before the decoration, before the tag, before '[',
    // and before '{' -- never between the tag and the depth digits. Pins
    // the grammar against a combinator refactor accidentally widening it.
    const std::string prefix = "// \\rSec";
    auto              r      = frontend::parse_rsec(prefix + " 2[foo]{Title}");
    REQUIRE(!r.has_value());
    CHECK(r.error().where.offset == prefix.size());
}

// --- malformed \rSec: positioned parse failures -----------------------------
// The gate: cover at minimum an out-of-range depth, a missing/malformed
// bracket, a non-numeric depth, and a missing stable name, and assert the
// positions, not just that they fail.

TEST_CASE("parse_rsec - an out-of-range depth is a positioned failure, not a throw") {
    // A std::stoi-style conversion would throw std::out_of_range here, a
    // latent crash no golden could cover (a crash cannot have a golden).
    // digits() reports the same input as a positioned parse_error instead.
    const std::string prefix = "// \\rSec";
    const std::string huge   = "99999999999"; // overflows int
    auto              r      = frontend::parse_rsec(prefix + huge + "[foo]{Title}");
    REQUIRE(!r.has_value());
    CHECK(r.error().where.offset == prefix.size());
    CHECK(r.error().message != nullptr);
}

TEST_CASE("parse_rsec - a missing open bracket after the depth is a positioned failure") {
    const std::string prefix = "// \\rSec2 ";
    auto              r      = frontend::parse_rsec(prefix + "no bracket here");
    REQUIRE(!r.has_value());
    CHECK(r.error().where.offset == prefix.size());
}

TEST_CASE("parse_rsec - a non-numeric depth is a positioned failure") {
    const std::string prefix = "// \\rSec";
    auto              r      = frontend::parse_rsec(prefix + "x[foo]{Title}");
    REQUIRE(!r.has_value());
    CHECK(r.error().where.offset == prefix.size());
}

TEST_CASE("parse_rsec - a missing stable name (no closing bracket) is a positioned failure") {
    // '[' opens but the input ends before a ']' terminates the stable name.
    const std::string raw = "// \\rSec2[foo";
    auto              r   = frontend::parse_rsec(raw);
    REQUIRE(!r.has_value());
    CHECK(r.error().where.offset == raw.size());
}

TEST_CASE("parse_rsec - an unterminated title (no closing '}') is a positioned failure") {
    const std::string raw = "// \\rSec2[foo]{Title";
    auto              r   = frontend::parse_rsec(raw);
    REQUIRE(!r.has_value());
    CHECK(r.error().where.offset == raw.size());
}
