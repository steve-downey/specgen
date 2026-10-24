// src/beman/specgen/rsec.cpp                                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/rsec.hpp>

#include <beman/specgen/foundation/parse/cursor.hpp>
#include <beman/specgen/foundation/parse/parser.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beman::specgen::grammar {

namespace parse = beman::specgen::foundation::parse;

namespace {

// Helper parsers for parse_rsec, below (decision parser-combinators). Not
// promoted to foundation/parse/parser.hpp: nothing else needs a `\rSec`
// grammar's blank-skipping or delimiter-content shape yet.

// Skips leading horizontal whitespace (space/tab only). Unlike
// foundation/parse/cursor.hpp's
// skip_intertoken_space(), this does not cross a newline — a `\rSec` marker
// is scanned within a single raw comment line, so a later line of a
// multi-line raw comment must never read as more leading whitespace before
// the tag.
constexpr auto skip_blank(parse::cursor cur) -> parse::cursor {
    while (!cur.empty() && (cur.peek() == ' ' || cur.peek() == '\t')) // substrate generic algorithm
        cur = cur.bump();
    return cur;
}

// A parser that always succeeds, skipping leading blanks and discarding its
// own value — the sequence_right glue at parse_rsec's four blank-tolerant
// points (before the decoration, before the tag,
// before '[', before '{').
[[nodiscard]] auto blanks() {
    return parse::parser{[](parse::cursor cur) -> parse::parse_result<bool> {
        return parse::parse_state<bool>{true, skip_blank(cur)};
    }};
}

// "//", "///", or "//!": the three raw-comment decorations
// RawComment::getRawText() may hand back. Always consumes the shared "//"
// first, then greedily consumes one more '/' or '!' if present — equivalent
// to trying the longest decoration first, since no value depends on which
// one matched.
[[nodiscard]] auto decoration() {
    return parse::sequence_right(parse::keyword("//"), parse::opt(parse::char_p('/') | parse::char_p('!')));
}

// Collects the run of characters strictly between an `open`/`close` pair,
// discarding the delimiters — the shared shape behind both `[stable]` and
// `{title}`. The run may be empty (e.g. `[]`); an unterminated run (no
// `close` before the input ends) is a positioned failure at end-of-input
// rather than a silent non-match.
[[nodiscard]] auto bracketed(char open, char close, const char* open_message, const char* close_message) {
    return parse::map(parse::sequence_right(
                          parse::satisfy([open](char c) { return c == open; }, open_message),
                          parse::sequence_left(
                              parse::many(parse::satisfy([close](char c) { return c != close; }, "delimiter content")),
                              parse::satisfy([close](char c) { return c == close; }, close_message))),
                      [](std::vector<char> chars) { return std::string(chars.begin(), chars.end()); });
}

// The `\rSec` tag alone -- decoration, blanks, and the literal "\rSec"
// keyword -- with no depth/[stable]/{title} grammar attached. parse_rsec,
// below, runs this as its own first step; rsec_tag_recognized() runs
// it again on its own so classify() can ask "did this comment even look
// like \rSec" independent of whether the rest of the marker goes on to
// parse, without duplicating the grammar by hand.
[[nodiscard]] auto rsec_tag() {
    return parse::sequence_right(
        blanks(), parse::sequence_right(decoration(), parse::sequence_right(blanks(), parse::keyword("\\rSec"))));
}

// Join a clang-format-wrapped `{title}` (issue #8): each newline, together
// with the continuation line's indentation and `//` decoration, reads as one
// space, so `{Accumulating applicative instance for\n// expected}` yields
// the title exactly as authored before the formatter wrapped it.
std::string unwrap_title(std::string_view title) {
    std::string out;
    out.reserve(title.size());
    std::size_t pos = 0;
    // substrate generic algorithm: a scan whose newline case consumes a
    // variable-length run (trailing blanks behind, indentation, decoration,
    // and leading blanks ahead) and emits a single joiner.
    while (pos < title.size()) {
        if (title[pos] != '\n') {
            out.push_back(title[pos]);
            ++pos;
            continue;
        }
        // substrate generic algorithm: trim the joiner's left side in place.
        while (!out.empty() && (out.back() == ' ' || out.back() == '\t' || out.back() == '\r'))
            out.pop_back();
        ++pos;
        // substrate generic algorithm: skip the continuation's indentation.
        while (pos < title.size() && (title[pos] == ' ' || title[pos] == '\t'))
            ++pos;
        if (pos + 1 < title.size() && title[pos] == '/' && title[pos + 1] == '/') {
            pos += 2;
            if (pos < title.size() && (title[pos] == '/' || title[pos] == '!'))
                ++pos;
            // substrate generic algorithm: skip the blanks after the
            // decoration.
            while (pos < title.size() && (title[pos] == ' ' || title[pos] == '\t'))
                ++pos;
        }
        if (!out.empty() && pos < title.size())
            out.push_back(' ');
    }
    return out;
}

} // namespace

parse::parse_result<SectionHeader> parse_rsec(std::string_view raw) {
    // \rSec<depth>[<stable>]{<title>}, tolerant of "//"/"///"/"//!"
    // decoration and horizontal whitespace at the points blanks() marks
    // below — never inside the brackets/braces, matching the grammar
    // classify() accepts. rsec_tag() is this same tag,
    // factored out so rsec_tag_recognized() can run it on its own.
    auto depth = parse::sequence_right(rsec_tag(), parse::digits());

    auto depth_and_stable =
        parse::lift2(depth,
                     parse::sequence_right(blanks(), bracketed('[', ']', "expected '['", "expected ']'")),
                     [](int d, std::string stable) { return std::pair{d, std::move(stable)}; });

    auto header =
        parse::lift2(depth_and_stable,
                     parse::sequence_right(blanks(), bracketed('{', '}', "expected '{'", "expected '}'")),
                     [](std::pair<int, std::string> depth_stable, std::string title) {
                         // The braced content may span physical lines when a
                         // formatter wrapped a long title (issue #8) — the
                         // comment splitter keeps such continuations in one
                         // item — so the value joins them back into the
                         // authored title.
                         return SectionHeader{depth_stable.first, std::move(depth_stable.second), unwrap_title(title)};
                     });

    return header(parse::cursor{raw});
}

bool rsec_tag_recognized(std::string_view raw) { return rsec_tag()(parse::cursor{raw}).has_value(); }

} // namespace beman::specgen::grammar
