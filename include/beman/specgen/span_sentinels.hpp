// include/beman/specgen/span_sentinels.hpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — token rewriting via valid-C++ sentinels (design §3.6).
// A rewrite replaces a token range with a `\exposid`/`\seebelow`/… span. Doing
// it directly would put backend markup (`@\exposid{…}@`) into text clang-format
// then mangles, so §3.6 splits it in three: (1) replace the range with a unique
// valid-C++ *sentinel* identifier, (2) format, (3) recover — swap each sentinel
// for its display text and record an ir::Span over it. This keeps the fragment
// parseable and lets clang-format line-break on a real token.
//
// Clang-free: the front end allocates sentinels and formats the text, and
// this component owns the sentinel names and their recovery into ir::Spans.

#ifndef BEMAN_SPECGEN_SPAN_SENTINELS_HPP
#define BEMAN_SPECGEN_SPAN_SENTINELS_HPP

#include <beman/specgen/ir.hpp>

#include <format>
#include <map>
#include <string>
#include <utility>

namespace beman::specgen::span_sentinels {

// The recovered form of one sentinel: `display` is what lands in CodeText::text
// (e.g. the exposid name `val`); `payload` drives the backend macro (the exposid
// name, or empty for \seebelow, whose macro takes no argument).
struct SpanInfo {
    beman::specgen::ir::SpanKind kind;
    std::string                  display;
    std::string                  payload;
};

// A unique, valid-C++ sentinel identifier for the n-th span in a fragment.
inline std::string span_sentinel(unsigned n) { return std::format("SPECGEN_{}_SPAN", n); }

// §3.6 step 3: replace each sentinel with its display text and record an
// ir::Span over it. Kept separate from formatting because derived conjuncts
// are expression fragments whose original layout is already the desired text.
beman::specgen::ir::CodeText recover_sentinels(std::string text, const std::map<std::string, SpanInfo>& sentinels);

// §3.6 step 1's "length-padded if needed so line-breaking stays honest", which
// the design has always specified and the sentinels never were. A sentinel
// stands in for its display text only until recovery, but clang-format decides
// line breaks and continuation alignment while it is still there -- so a name
// whose exposition-only spelling is a different width came out aligned to the
// sentinel's width instead of its own, and a continuation under an opening `<`
// lined up with nothing (issue #67).
//
// Resizing them here rather than at each of the twenty allocation sites keeps
// the one rule in one place: a sentinel is exactly as wide as what replaces it.
// It also has to be here, because it is the only point that holds the whole
// fragment, which is what makes "this identifier appears nowhere else"
// checkable rather than assumed.
std::pair<std::string, std::map<std::string, SpanInfo>>
resize_sentinels(std::string text, const std::map<std::string, SpanInfo>& sentinels);

} // namespace beman::specgen::span_sentinels

#endif // BEMAN_SPECGEN_SPAN_SENTINELS_HPP
