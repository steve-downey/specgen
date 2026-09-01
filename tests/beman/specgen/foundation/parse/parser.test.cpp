// tests/beman/specgen/foundation/parse/parser.test.cpp          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Ported from compile-time-scheme's src/smd/smdscheme/parser/parser.test.cpp
// and alt.test.cpp (decision parser-combinators), adapted to the
// std::expected
// carrier and std::vector-collecting repetition combinators, plus new
// coverage upstream has none of: ordered-choice commit-on-consumed
// semantics, and digits() overflow. keyword() has no upstream equivalent (see
// the provenance note in parser.hpp) so its tests are new outright.

#include <beman/specgen/foundation/parse/parser.hpp>
#include <beman/specgen/foundation/parse/parser.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>

using beman::specgen::foundation::parse::char_p;
using beman::specgen::foundation::parse::cursor;
using beman::specgen::foundation::parse::digits;
using beman::specgen::foundation::parse::keyword;
using beman::specgen::foundation::parse::lexeme;
using beman::specgen::foundation::parse::lift2;
using beman::specgen::foundation::parse::many;
using beman::specgen::foundation::parse::map;
using beman::specgen::foundation::parse::opt;
using beman::specgen::foundation::parse::pure;
using beman::specgen::foundation::parse::satisfy;
using beman::specgen::foundation::parse::sequence_left;
using beman::specgen::foundation::parse::sequence_right;
using beman::specgen::foundation::parse::some;

// --- ported: applicative primitives (parser.hpp) ----------------------------

static_assert(char_p('x')(cursor{"xyz"}).has_value());
static_assert(char_p('x')(cursor{"xyz"})->value == 'x');
static_assert(char_p('x')(cursor{"xyz"})->rest.peek() == 'y');

static_assert(pure(42)(cursor{"abc"}).has_value());
static_assert(pure(42)(cursor{"abc"})->value == 42);
static_assert(pure(42)(cursor{"abc"})->rest.remaining() == "abc");

static_assert(!char_p('x')(cursor{"abc"}).has_value());
static_assert(!char_p('x')(cursor{""}).has_value());

static_assert(!satisfy([](char c) { return c == 'z'; }, "z")(cursor{""}).has_value());

static_assert(map(char_p('x'), [](char c) { return int(c); })(cursor{"xyz"}).has_value());
static_assert(map(char_p('x'), [](char c) { return int(c); })(cursor{"xyz"})->value == int('x'));
static_assert(!map(char_p('x'), [](char c) { return int(c); })(cursor{"abc"}).has_value());

static_assert(
    lift2(char_p('a'), char_p('b'), [](char a, char b) { return a == 'a' && b == 'b'; })(cursor{"ab"})->value == true);

static_assert(sequence_left(char_p('a'), char_p('b'))(cursor{"ab"})->value == 'a');
static_assert(sequence_left(char_p('a'), char_p('b'))(cursor{"ab"})->rest.remaining() == "");
static_assert(sequence_right(char_p('a'), char_p('b'))(cursor{"ab"})->value == 'b');

// --- ported: ordered choice and repetition (alt.hpp) ------------------------

static_assert((char_p('a') | char_p('b'))(cursor{"b"}).has_value());
static_assert((char_p('a') | char_p('b'))(cursor{"b"})->value == 'b');
static_assert(!(char_p('a') | char_p('b'))(cursor{"c"}).has_value());
static_assert((char_p('a') | char_p('b'))(cursor{"a"})->value == 'a');

static_assert(many(char_p('a'))(cursor{""})->value.empty());
static_assert(many(char_p('a'))(cursor{"aaa"})->value.size() == 3);

static_assert(!some(char_p('a'))(cursor{""}).has_value());
static_assert(some(char_p('a'))(cursor{"aa"})->value.size() == 2);

static_assert(opt(char_p('a'))(cursor{"b"}).has_value());
static_assert(!opt(char_p('a'))(cursor{"b"})->value.has_value());
static_assert(opt(char_p('a'))(cursor{"a"})->value.has_value());

static_assert(lexeme(char_p('x'))(cursor{"  x  "})->value == 'x');

TEST_CASE("parser - char_p matches expected") {
    constexpr cursor c{"xyz"};
    constexpr auto   r = char_p('x')(c);
    REQUIRE(r.has_value());
    CHECK(r->value == 'x');
    CHECK(r->rest.peek() == 'y');
}

TEST_CASE("parser - char_p mismatch and empty cursor both fail") {
    CHECK(!char_p('x')(cursor{"abc"}).has_value());
    CHECK(!char_p('x')(cursor{""}).has_value());
}

TEST_CASE("parser - pure succeeds with unchanged cursor") {
    constexpr cursor c{"hello"};
    constexpr auto   r = pure(42)(c);
    REQUIRE(r.has_value());
    CHECK(r->value == 42);
    CHECK(r->rest.remaining() == "hello");
}

TEST_CASE("parser - map propagates failure") {
    CHECK(!map(char_p('x'), [](char ch) { return int(ch); })(cursor{"abc"}).has_value());
}

TEST_CASE("parser - lift2 forwards the earliest error") {
    // pb never runs because pa already failed.
    auto p = lift2(char_p('a'), char_p('b'), [](char a, char b) { return a == 'a' && b == 'b'; });
    auto r = p(cursor{"xy"});
    REQUIRE(!r.has_value());
    CHECK(r.error().where.offset == 0);
}

TEST_CASE("parser - sequence_right keeps the right value") {
    auto r = sequence_right(char_p('a'), char_p('b'))(cursor{"ab"});
    REQUIRE(r.has_value());
    CHECK(r->value == 'b');
}

TEST_CASE("parser - many collects zero or more into a vector") {
    auto p = many(char_p('a'));
    CHECK(p(cursor{""})->value.empty());
    CHECK(p(cursor{"aaa"})->value.size() == 3);
    CHECK(p(cursor{"aaaab"})->value.size() == 4);
    CHECK(p(cursor{"aaaab"})->rest.peek() == 'b');
}

TEST_CASE("parser - some requires at least one match") {
    auto p = some(char_p('a'));
    CHECK(!p(cursor{""}).has_value());
    CHECK(p(cursor{"aa"})->value.size() == 2);
}

TEST_CASE("parser - opt yields nullopt without consuming on failure") {
    auto p = opt(char_p('a'));
    auto r = p(cursor{"b"});
    REQUIRE(r.has_value());
    CHECK(!r->value.has_value());
    CHECK(r->rest.remaining() == "b"); // no input consumed
    auto r2 = p(cursor{"a"});
    REQUIRE(r2->value.has_value());
    CHECK(*r2->value == 'a');
}

TEST_CASE("parser - lexeme strips surrounding whitespace") {
    auto p = lexeme(char_p('x'));
    CHECK(p(cursor{"  x  "})->value == 'x');
    CHECK(p(cursor{"x"})->value == 'x');
    CHECK(!p(cursor{"  y"}).has_value());
}

// --- new: ordered-choice commit-on-consumed-input semantics -----------------
// The subtlest piece of the combinator set — tested
// directly here rather than only incidentally through keyword().

TEST_CASE("operator| - untried alternative when the first fails at the start position") {
    auto p = char_p('a') | char_p('b');
    auto r = p(cursor{"b"});
    REQUIRE(r.has_value());
    CHECK(r->value == 'b');
}

TEST_CASE("operator| - a committed (consumed) failure is final, second branch never runs") {
    // ab| ac: "ab" consumes 'a' then fails matching 'c' against input "ac" —
    // one character in, so per commit-on-consumed-input the "ac" alternative
    // is never tried even though it would have matched.
    auto ab = sequence_right(char_p('a'), char_p('b'));
    auto ac = sequence_right(char_p('a'), char_p('c'));
    auto p  = ab | ac;
    auto r  = p(cursor{"ac"});
    REQUIRE(!r.has_value());
    // The error position is after the consumed 'a' (offset 1): ab's failure,
    // not a report from an untried ac.
    CHECK(r.error().where.offset == 1);
}

TEST_CASE("operator| - keyword sharing a prefix: partial match commits to the failure") {
    // \rSec vs \ref both start with '\r'... use two keywords sharing a
    // literal prefix to exercise the same rule parse_rsec depends
    // on: matching "\\rSec" against "\\rX" must not silently fall through to
    // an alternative "\\rX"-shaped tag once "\\r" has been consumed.
    auto rsec = keyword("\\rSec");
    auto rref = keyword("\\rXYZ");
    auto p    = rsec | rref;
    auto r    = p(cursor{"\\rXYZ"});
    // rsec consumes '\' and 'r' before mismatching at 'X' vs 'S' — committed,
    // so rref (which would have matched) is never attempted.
    REQUIRE(!r.has_value());
    CHECK(r.error().where.offset == 2);
}

TEST_CASE("operator| - no shared prefix: first branch's uncommitted failure allows fallback") {
    // Genuinely prefix-disjoint keywords: the first character itself
    // mismatches, so nothing has been consumed and the second alternative is
    // tried. (Sibling docblock markers like \pre/\post/\at all share the "\"
    // prefix, so chaining keyword(...) | keyword(...) directly over them
    // would commit on that shared backslash and never reach the sibling —
    // that grammar instead scans the identifier once with many()/satisfy()
    // and dispatches the resulting name through a table, the same shape
    // parse_docblock's find_marker already uses.)
    auto p = keyword("cat") | keyword("dog");
    auto r = p(cursor{"dog house"});
    REQUIRE(r.has_value());
    CHECK(r->value == "dog");
    CHECK(r->rest.remaining() == " house");
}

// --- new: keyword() -----------------------------------------------------

TEST_CASE("keyword - matches the exact spelling and consumes it") {
    auto r = keyword("\\rSec")(cursor{"\\rSec2[foo]"});
    REQUIRE(r.has_value());
    CHECK(r->value == "\\rSec");
    CHECK(r->rest.remaining() == "2[foo]");
}

TEST_CASE("keyword - mismatch on the very first character does not consume") {
    auto r = keyword("\\rSec")(cursor{"x"});
    REQUIRE(!r.has_value());
    CHECK(r.error().where.offset == 0);
}

TEST_CASE("keyword - empty input fails without consuming") {
    auto r = keyword("\\rSec")(cursor{""});
    REQUIRE(!r.has_value());
    CHECK(r.error().where.offset == 0);
}

// --- new: digits() --------------------------------------------------------

TEST_CASE("digits - parses a run of decimal digits") {
    auto r = digits()(cursor{"123abc"});
    REQUIRE(r.has_value());
    CHECK(r->value == 123);
    CHECK(r->rest.remaining() == "abc");
}

TEST_CASE("digits - fails without consuming when no digit is present") {
    auto r = digits()(cursor{"abc"});
    REQUIRE(!r.has_value());
    CHECK(r.error().where.offset == 0);
}

TEST_CASE("digits - fails on empty input") { CHECK(!digits()(cursor{""}).has_value()); }

TEST_CASE("digits - a single leading zero parses as zero") {
    auto r = digits()(cursor{"0]"});
    REQUIRE(r.has_value());
    CHECK(r->value == 0);
    CHECK(r->rest.remaining() == "]");
}

TEST_CASE("digits - out-of-range value is a positioned parse failure, not a throw") {
    // The motivating case for digits() (decision parser-combinators):
    // std::stoi throws out_of_range for a digit run like this; digits() must not.
    const std::string huge = std::to_string(static_cast<long long>(std::numeric_limits<int>::max()) + 1) + "]";
    auto              r    = digits()(cursor{huge});
    REQUIRE(!r.has_value());
    CHECK(r.error().where.offset == 0);
    CHECK(r.error().message != nullptr);
}

TEST_CASE("digits - max int value parses successfully") {
    const std::string spelling = std::to_string(std::numeric_limits<int>::max());
    auto              r        = digits()(cursor{spelling});
    REQUIRE(r.has_value());
    CHECK(r->value == std::numeric_limits<int>::max());
    CHECK(r->rest.empty());
}

// --- new: a small parse_rsec-shaped grammar, combined -----------------------
// Exercises pure/keyword/digits/char_p/many/map/lift2 together the way
// parse_rsec does: `\rSec<digits>[<stable>]`.

TEST_CASE("combined - keyword+digits+char_p+many composes into a marker header parse") {
    auto stable_char = satisfy([](char c) { return c != ']'; }, "stable name char");
    auto stable_name =
        map(many(stable_char), [](const std::vector<char>& cs) { return std::string(cs.begin(), cs.end()); });
    auto header = sequence_right(keyword("\\rSec"),
                                 lift2(digits(),
                                       sequence_right(char_p('['), sequence_left(stable_name, char_p(']'))),
                                       [](int depth, std::string name) { return std::pair{depth, std::move(name)}; }));

    auto r = header(cursor{"\\rSec2[algorithms.sort]{Sorting}"});
    REQUIRE(r.has_value());
    CHECK(r->value.first == 2);
    CHECK(r->value.second == "algorithms.sort");
    CHECK(r->rest.remaining() == "{Sorting}");
}
