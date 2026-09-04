// tests/beman/specgen/frontend/diagnostics.test.cpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// build_document() has a diagnostic channel, so classify() can report a
// malformed `\rSec` instead of silently dropping it. An over-large depth is
// a positioned parse failure rather than a crash, and the channel is where
// that failure surfaces instead of the comment being skipped unremarked;
// this file is where that behavior is pinned.
//
// The distinction under test is commit-on-consumed-input, applied at the
// `\rSec` tag: a comment whose tag never matched is not a structure marker
// at all (a `\ref` group header, license/SPDX text, a `//!` docblock) and
// must stay silent, while a comment whose tag matched and whose
// depth/`[stable]`/`{title}` grammar then failed is a malformed marker and
// is reported. Both halves are asserted here, because only asserting the
// first would pass just as well for a front end that reported everything.
//
// Also pinned: reporting a diagnostic changes nothing about which nodes land
// in the document — the channel is additive.

#include <beman/specgen/document_build.hpp>
#include <beman/specgen/frontend/frontend.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace db       = beman::specgen::document_build;
namespace frontend = beman::specgen::frontend;
namespace ir       = beman::specgen::ir;

namespace {

const std::string kDiagnosticsHeader = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_diagnostics.hpp";
const std::string kCoalescedHeader   = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_rsec_coalesced.hpp";
const std::string kWidgetHeader      = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_widget.hpp";

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

// The malformed-`\rSec` half of the channel, separated from the
// docblock half that shares it. Both halves come out of one vector in
// source order; a test about one of them selects it rather than counting
// past the other.
std::vector<db::Diagnostic> marker_diagnostics(const std::vector<db::Diagnostic>& all) {
    return all | std::views::filter([](const db::Diagnostic& d) { return contains(d.message, "malformed \\rSec"); }) |
           std::ranges::to<std::vector<db::Diagnostic>>();
}

std::vector<db::Diagnostic> docblock_diagnostics(const std::vector<db::Diagnostic>& all) {
    return all | std::views::filter([](const db::Diagnostic& d) {
               return !contains(d.message, "malformed \\rSec") && !contains(d.message, "unrecognized section header");
           }) |
           std::ranges::to<std::vector<db::Diagnostic>>();
}

std::vector<db::Diagnostic> unrecognized_header_diagnostics(const std::vector<db::Diagnostic>& all) {
    return all | std::views::filter([](const db::Diagnostic& d) {
               return contains(d.message, "unrecognized section header");
           }) |
           std::ranges::to<std::vector<db::Diagnostic>>();
}

} // namespace

// --- malformed markers are reported ----------------------------------------

TEST_CASE("build_document - a malformed \\rSec is reported as a diagnostic, not silently skipped") {
    const auto built = frontend::build_document(kDiagnosticsHeader);
    REQUIRE(built.has_value());

    // spec_diagnostics.hpp holds exactly two malformed markers: an
    // out-of-range depth and a marker with no `{title}`. Every other comment
    // in the file fails parse_rsec too, and none of those may be counted.
    // Selected by message: the same channel also carries the header's
    // docblock findings, which are the next section's business.
    const std::vector<db::Diagnostic> markers = marker_diagnostics(built->diagnostics);
    REQUIRE(markers.size() == 2);

    // Collected in the order build_tree visits events, which is source
    // offset order, so the out-of-range depth comes first.
    CHECK(contains(markers[0].message, "out of range"));

    CHECK(markers[0].line < markers[1].line);
    // The line is the malformed comment's own place in the file, not the
    // start of the file: something later than the well-formed `\rSec3`.
    CHECK(markers[0].line > 0);
    // A malformed marker is a Warning: the comment contributes no node either
    // way, so nothing downstream is wrong, but the author meant it to.
    CHECK(markers[0].severity == beman::specgen::Severity::Warning);
}

// --- non-matches stay silent ------------------------------------------------

TEST_CASE("build_document - a \\ref group header and license/SPDX text produce no diagnostic") {
    // spec_widget.hpp is the plainest corpus header: an SPDX line, a prose
    // block, two `\ref{...}` group headers in the class body, `//!` docblocks,
    // and only well-formed `\rSec3` markers. Every one of those comments
    // fails parse_rsec except the two markers, and not one of them is an
    // error. The `\ref` headers are the case that matters: they share
    // `\rSec`'s leading `\r`, so parse_rsec fails at offset 5 rather than at
    // offset 0 -- a check that merely asked whether the failure position had
    // moved off the start of the comment would report all four of them.
    const auto built = frontend::build_document(kWidgetHeader);
    REQUIRE(built.has_value());
    CHECK(built->diagnostics.empty());
}

TEST_CASE("parse_rsec - a non-match's failure offset is not the start of the comment") {
    // The trap this classification has to avoid, pinned as executable evidence.
    //
    // The obvious reading of commit-on-consumed-input is "the failure offset
    // moved off the start, so input was consumed, so the comment committed to
    // being a `\rSec`". That is wrong here, because the *tag* is not just
    // `\rSec`: it is decoration, blanks, and then the keyword. Every ordinary
    // C++ comment consumes its `//` before the keyword ever runs, so a plain
    // non-match already fails at a nonzero offset -- and a `\ref` header goes
    // further still, matching `\` and `r` before `S` fails.
    //
    // The boundary that actually separates "not a `\rSec` at all" from
    // "malformed `\rSec`" is therefore the tag parser's own success, not a
    // comparison against offset 0. classify() asks exactly that question.
    CHECK(frontend::parse_rsec("// \\ref{gadget.cons}, constructors").error().where.offset == 5);
    CHECK(frontend::parse_rsec("// namespace demo").error().where.offset == 3);
    CHECK(frontend::parse_rsec("// SPDX-License-Identifier: Apache-2.0").error().where.offset == 3);

    // The one shape that does fail at offset 0 is a comment with no
    // decoration at all -- which is the case that never reaches classify().
    CHECK(frontend::parse_rsec("\\rSec2[foo]{Title}").error().where.offset == 0);

    // A malformed marker, by contrast, fails past the end of the tag.
    const auto malformed = frontend::parse_rsec("// \\rSec3[gadget.untitled]");
    REQUIRE_FALSE(malformed.has_value());
    CHECK(malformed.error().where.offset > std::string_view("// \\rSec").size());
}

TEST_CASE("build_document - a numbered section-like header is reported instead of silently filing onward") {
    const auto built = frontend::build_document(kDiagnosticsHeader);
    REQUIRE(built.has_value());

    const std::vector<db::Diagnostic> found = unrecognized_header_diagnostics(built->diagnostics);
    REQUIRE(found.size() == 1);
    CHECK(found[0].severity == beman::specgen::Severity::Warning);
    CHECK(found[0].message == "unrecognized section header [gadget.mod]; use \\rSec<depth>[gadget.mod]{title}");

    // It is still ignored rather than guessed into a Section: without a
    // depth, opening a frame would trade a visible warning for invented
    // structure. The pre-existing synopsis, the class's own description and
    // the two valid sections stay put.
    CHECK(built->document.nodes.size() == 4);
}

TEST_CASE("build_document - section markers retain physical lines inside a coalesced RawComment") {
    const auto collected = frontend::collect_interleaved(kCoalescedHeader);
    REQUIRE_FALSE(collected.had_parse_error);
    const auto merged = std::ranges::find_if(collected.items, [](const frontend::SourceItem& item) {
        return item.kind == frontend::SourceItem::Kind::Comment && item.label.contains("coalesced.first");
    });
    REQUIRE(merged != collected.items.end());
    CHECK(merged->label.contains("Clang presents this prelude"));
    CHECK(merged->label.contains("coalesced.second"));
    CHECK(merged->label.contains("coalesced.malformed"));

    const auto built = frontend::build_document(kCoalescedHeader);
    REQUIRE(built.has_value());
    REQUIRE(built->document.nodes.size() == 2);

    const auto* first = std::get_if<ir::Section>(&built->document.nodes[0]);
    REQUIRE(first != nullptr);
    CHECK(first->stable_name == "coalesced.first");
    CHECK(first->title == "First");

    const auto* second = std::get_if<ir::Section>(&built->document.nodes[1]);
    REQUIRE(second != nullptr);
    CHECK(second->stable_name == "coalesced.second");
    CHECK(second->title == "Second");

    const std::vector<db::Diagnostic> markers = marker_diagnostics(built->diagnostics);
    REQUIRE(markers.size() == 1);
    CHECK(markers.front().line == 9);
    CHECK(contains(markers.front().message, "expected '{'"));
}

// --- docblock findings reach the same channel -------------------------------
//
// The rules these exercise are grammar::parse_docblock's and are unit-tested
// in tests/beman/specgen/docblock.test.cpp. What these pin is that a finding
// computed there *arrives* somewhere, positioned in the file rather than in
// the docblock, from each of the three events that can carry one.
// `golden.diagnostics` pins the printed text of the same run.

TEST_CASE("build_document - a docblock's findings are reported, positioned in the file") {
    const auto built = frontend::build_document(kDiagnosticsHeader);
    REQUIRE(built.has_value());

    const std::vector<db::Diagnostic> found = docblock_diagnostics(built->diagnostics);
    REQUIRE(found.size() == 4);

    // `gadget`'s own docblock, misspelling \remarks (the SynopsisDecl channel
    // by its class-definition route, issue #18): the class produces a
    // synopsis either way, so nothing else here would have shown the typo.
    CHECK(found[0].severity == beman::specgen::Severity::Error);
    CHECK(contains(found[0].message, "unknown tag \\remark"));

    // The hidden friend's duplicate \effects (the SynopsisDecl channel: an
    // in-class member's markup is parsed while walking the class body).
    CHECK(found[1].severity == beman::specgen::Severity::Warning);
    CHECK(contains(found[1].message, "duplicate \\effects element"));

    // gadget::value()'s \effects after its \remarks (the ItemDecl channel) --
    // a mistake the canonicalized output hides, so only this report shows it.
    CHECK(found[2].severity == beman::specgen::Severity::Note);
    CHECK(contains(found[2].message, "\\effects appears after \\remarks"));

    // The \omit'ed gadget::scratch()'s misspelled tag (the Ignored channel):
    // the entity is deliberately unspecified, the typo is not.
    CHECK(found[3].severity == beman::specgen::Severity::Error);
    CHECK(contains(found[3].message, "unknown tag \\effect"));

    // Each line is the docblock line the finding is about, translated out of
    // the docblock's own 1-based numbering into the file's. Nothing here
    // hard-codes a line number -- the header is edited too often for that --
    // but they are strictly increasing down the file, and the first is well
    // past the license block.
    CHECK(found[0].line > 10);
    CHECK(found[0].line < found[1].line);
    CHECK(found[1].line < found[2].line);
    CHECK(found[2].line < found[3].line);
}

// --- the channel is additive ------------------------------------------------

TEST_CASE("build_document - diagnostics do not change which nodes land in the document") {
    const auto built = frontend::build_document(kDiagnosticsHeader);
    REQUIRE(built.has_value());
    REQUIRE_FALSE(built->diagnostics.empty());

    // The class synopsis, the class's own description, and the two
    // well-formed `\rSec3` sections: exactly what this header produces with
    // the diagnostics disregarded, since a malformed marker is skipped, and
    // so is the `\omit`ed definition whose docblock is reported above. The
    // reported class docblock still contributes its good element, which is
    // the description node.
    REQUIRE(built->document.nodes.size() == 4);
    CHECK(std::holds_alternative<ir::Synopsis>(built->document.nodes[0]));
    const auto* description = std::get_if<ir::SpecItem>(&built->document.nodes[1]);
    REQUIRE(description != nullptr);
    CHECK(description->decl.signatures.empty());
    REQUIRE(description->descr.elements.size() == 1);
    CHECK(description->descr.elements[0].kind == ir::ElementKind::Remarks);

    const auto* cons = std::get_if<ir::Section>(&built->document.nodes[2]);
    REQUIRE(cons != nullptr);
    CHECK(cons->stable_name == "gadget.cons");
    CHECK(cons->title == "Constructors");

    const auto* obs = std::get_if<ir::Section>(&built->document.nodes[3]);
    REQUIRE(obs != nullptr);
    CHECK(obs->stable_name == "gadget.obs");
    // The reported entities are still described: the duplicate-\effects
    // hidden friend and the out-of-order gadget::value(). A finding is a
    // remark about the wording, not a veto on it.
    CHECK(obs->children.size() == 2);
}
