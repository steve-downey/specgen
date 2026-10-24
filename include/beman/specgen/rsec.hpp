// include/beman/specgen/rsec.hpp                                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — the `\rSec<depth>[<stable>]{<title>}` section-marker grammar
// (design §3.2). Like the docblock grammar beside it, a pure string parser:
// the front end hands it raw comment text and turns what it returns into
// document structure.

#ifndef BEMAN_SPECGEN_RSEC_HPP
#define BEMAN_SPECGEN_RSEC_HPP

#include <beman/specgen/parse/parser.hpp>

#include <string>
#include <string_view>

namespace beman::specgen::grammar {

// A parsed `\rSec<depth>[<stable>]{<title>}` structure marker (design §3.2).
struct SectionHeader {
    int         depth = 0;
    std::string stable;
    std::string title;
};

// Recognizes a `\rSec<depth>[<stable>]{<title>}` structure marker in `raw`,
// the raw comment text verbatim (decoration and all, as
// RawComment::getRawText() returns it -- see SourceItem::label). Tolerant of
// "//", "///", or "//!" decoration and horizontal whitespace (never a
// newline) around the tag and its brackets; the bracket/brace contents
// themselves are taken verbatim. Built on the combinator library
// (decision parser-combinators, parse/{cursor,parser}.hpp):
//
//  - Failing before any input is consumed means "not a \rSec comment at
//    all" -- a `\ref{...}` synopsis-group header, license/SPDX text, a
//    trailing-brace comment. These are not errors.
//  - Failing after the tag is recognized -- a missing/malformed bracket, a
//    non-numeric or out-of-range depth, a missing stable name -- reports a
//    positioned parse_error instead. An out-of-range depth is one of these
//    positioned failures like any other, never a crash.
//
// build_document()'s classify() step (decision document-build-stages) turns
// every failure into a document_build::Ignored event either way -- the
// comment is not structure, so no node is added in either case -- but it
// does not treat them alike: an
// ordinary non-match stays a bare Ignored{}, while a malformed marker's
// Ignored carries a document_build::Diagnostic built from the position above,
// which build_document()'s caller can surface instead of silently dropping.
// A numbered draft-style heading ending in a dotted `[stable.name]` gets the
// same channel: it is not parsed as structure, but a Warning reports it
// rather than silently leaving the preceding section open.
beman::specgen::parse::parse_result<SectionHeader> parse_rsec(std::string_view raw);

// True once `raw`'s \rSec tag itself -- decoration, blanks, and the literal
// "\rSec" -- has matched, independent of whether the depth/[stable]/{title}
// grammar after it goes on to parse. classify() calls this only when
// parse_rsec(raw) has already failed, to tell apart the two ways that can
// happen: the tag never matched (false -- "not a \rSec at all"; a `\ref`
// group header shares \rSec's leading "\r", but the keyword comparison still
// fails), or the tag matched and something after it did not (true -- a
// malformed \rSec). It runs the same tag parser parse_rsec does, so it is the
// tag's own success or failure, not a re-derivation by comparing parse_error
// offsets against a guessed constant, which a marker sharing a literal prefix
// with "\rSec" (like `\ref`) would get wrong.
bool rsec_tag_recognized(std::string_view raw);

} // namespace beman::specgen::grammar

#endif // BEMAN_SPECGEN_RSEC_HPP
