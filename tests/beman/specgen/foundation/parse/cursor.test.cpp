// tests/beman/specgen/foundation/parse/cursor.test.cpp          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Ported from compile-time-scheme's
// src/smd/smdscheme/parser/cursor.test.cpp (decision parser-combinators),
// adapted for the runtime-ized cursor (no is_initial_symbol_char/
// is_symbol_char/is_delimiter here — those are Scheme-lexer specific and have
// no specgen grammar to serve) plus coverage for multi-line
// offset/line/column tracking.

#include <beman/specgen/foundation/parse/cursor.hpp>
#include <beman/specgen/foundation/parse/cursor.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

using beman::specgen::foundation::parse::cursor;
using beman::specgen::foundation::parse::is_space;
using beman::specgen::foundation::parse::skip_intertoken_space;
using beman::specgen::foundation::parse::source_pos;

static_assert([] {
    cursor c{"  x"};
    return skip_intertoken_space(c).peek() == 'x';
}());

TEST_CASE("cursor - EmptyInput") {
    constexpr cursor c{""};
    STATIC_REQUIRE(c.empty());
}

TEST_CASE("cursor - PeekAtFirst") {
    constexpr cursor c{"abc"};
    STATIC_REQUIRE(c.peek() == 'a');
}

TEST_CASE("cursor - BumpAdvancesOffset") {
    constexpr cursor c{"ab"};
    constexpr cursor c2 = c.bump();
    STATIC_REQUIRE(c2.position().offset == 1);
    STATIC_REQUIRE(c2.position().column == 2);
    STATIC_REQUIRE(c2.peek() == 'b');
}

TEST_CASE("cursor - BumpOnNewlineUpdatesLineAndResetsColumn") {
    constexpr cursor c{"a\nb"};
    constexpr cursor after_a  = c.bump();
    constexpr cursor after_nl = after_a.bump();
    STATIC_REQUIRE(after_nl.position().line == 2);
    STATIC_REQUIRE(after_nl.position().column == 1);
    STATIC_REQUIRE(after_nl.peek() == 'b');
}

TEST_CASE("cursor - PositionStartsAtLine1Col1Offset0") {
    constexpr cursor c{"x"};
    STATIC_REQUIRE(c.position() == source_pos{.offset = 0, .line = 1, .column = 1});
}

TEST_CASE("cursor - RemainingReturnsUnconsumedInput") {
    constexpr cursor c{"hello"};
    constexpr cursor c2 = c.bump();
    STATIC_REQUIRE(c2.remaining() == "ello");
}

TEST_CASE("cursor - IsSpace") {
    STATIC_REQUIRE(is_space(' '));
    STATIC_REQUIRE(is_space('\t'));
    STATIC_REQUIRE(is_space('\n'));
    STATIC_REQUIRE(is_space('\r'));
    STATIC_REQUIRE(!is_space('a'));
    STATIC_REQUIRE(!is_space('('));
}

TEST_CASE("cursor - SkipInterTokenSpaceStopsAtNonSpace") {
    constexpr cursor c{"   foo"};
    constexpr cursor result = skip_intertoken_space(c);
    STATIC_REQUIRE(result.peek() == 'f');
    STATIC_REQUIRE(result.position().offset == 3);
}

TEST_CASE("cursor - SkipInterTokenSpaceOnEmptyInput") {
    constexpr cursor c{""};
    constexpr cursor result = skip_intertoken_space(c);
    STATIC_REQUIRE(result.empty());
}

// --- specgen-specific: line/column tracking across several newlines --------
// parse_rsec and the docblock
// scanners report diagnostics by line/column, so multi-line tracking
// through bump() has to be exact, not just correct for a single "\n".

TEST_CASE("cursor - MultilineTrackingAdvancesLineAndColumnPerLine") {
    // "ab\ncd\nef": three lines of two characters each.
    cursor c{"ab\ncd\nef"};
    REQUIRE(c.position() == source_pos{.offset = 0, .line = 1, .column = 1});

    c = c.bump(); // 'a' consumed, now at 'b'
    REQUIRE(c.position() == source_pos{.offset = 1, .line = 1, .column = 2});

    c = c.bump(); // 'b' consumed, now at '\n'
    REQUIRE(c.position() == source_pos{.offset = 2, .line = 1, .column = 3});

    c = c.bump(); // '\n' consumed, now at 'c' on line 2
    REQUIRE(c.position() == source_pos{.offset = 3, .line = 2, .column = 1});
    REQUIRE(c.peek() == 'c');

    c = c.bump(); // 'c' consumed
    c = c.bump(); // 'd' consumed, now at second '\n'
    REQUIRE(c.position() == source_pos{.offset = 5, .line = 2, .column = 3});

    c = c.bump(); // second '\n' consumed, now at 'e' on line 3
    REQUIRE(c.position() == source_pos{.offset = 6, .line = 3, .column = 1});
    REQUIRE(c.peek() == 'e');
}

TEST_CASE("cursor - ConsecutiveNewlinesEachResetColumnAndAdvanceLine") {
    cursor c{"\n\n\nx"};
    for (int expected_line = 1; expected_line <= 3; ++expected_line) {
        REQUIRE(c.position().line == expected_line);
        REQUIRE(c.position().column == 1);
        c = c.bump();
    }
    REQUIRE(c.position().line == 4);
    REQUIRE(c.position().column == 1);
    REQUIRE(c.peek() == 'x');
}

TEST_CASE("cursor - BumpIsImmutableCheckpointAndBacktrack") {
    // bump() returns a new cursor; the original is left untouched, so a
    // parser can checkpoint before a tentative advance and resume from the
    // checkpoint if the tentative path fails (decision parser-combinators).
    const cursor checkpoint{"xy"};
    const cursor advanced = checkpoint.bump();
    REQUIRE(checkpoint.peek() == 'x');
    REQUIRE(checkpoint.position().offset == 0);
    REQUIRE(advanced.peek() == 'y');
    REQUIRE(advanced.position().offset == 1);
}
