// include/beman/specgen/document_build.hpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The clang-free half of frontend.cpp's document-tree builder (decision
// document-build-stages). Building a document is four jobs: classify a raw
// item, fold `\rSec` depths into nested sections, group `\also`/empty-descr
// followers onto their primary, and route in-class members to the section
// their `\ref` group names. The build splits these across three stages:
//
//  1. classify(RawItem) -> DocEvent (frontend.cpp, Tier B) — the only place
//     that touches a clang::Decl*, quarantining the dyn_cast if/else chain
//     inside one function that converts Clang's world into the closed
//     DocEvent variant below.
//  2. build_tree(span<DocEvent>) -> ir::Document (this header) — the
//     `\rSec` frame stack.
//  3. group_items(vector<GroupCandidate>) -> vector<GroupCandidate> (this
//     header) — the `\also`/empty-descr grouping.
//
// **Why stage 3 is not `group_items(ir::Document) -> ir::Document` — read
// before changing either function.** That shape — a post-pass run once over
// the fully built tree — would have to re-derive the `\also`/empty-descr
// *intent* from the tree's own shape, e.g. by having classify()
// unconditionally clear a would-be follower's descr and having group_items
// treat "empty descr next to a SpecItem" as the join signal. Two problems
// rule that out:
//
//  (a) That clearing is lossy. An `\also` item that carries real
//      description content and turns out to have no primary to join (no
//      preceding SpecItem sibling) keeps that content; clearing it
//      up front regardless of whether a join actually happens throws it
//      away. "Does this attach" and "does this item have content" are
//      independent facts and the tree has nowhere else to carry the first
//      one once it is separated from the second.
//  (b) Timing, not just data, would be wrong. A join is decided against
//      `stack.back().children.back()` *as each item is
//      pushed* — i.e. in push (source interleave) order — and only
//      afterwards, at frame close, is a frame's children stable-sorted into
//      placement-key order (an out-of-line definition's key is its
//      *in-class declaration's* offset, which routinely differs from its
//      own push position once in-class members interleave with out-of-line
//      siblings). Evaluating "attaches to the previous node" on an
//      *already-sorted* ir::Document, as a genuine post-pass, asks the
//      question of the wrong neighbor whenever push order and sorted order
//      disagree — which they provably do, not just could.
//
// So the join decision is threaded through explicitly instead of
// re-derived from tree shape: classify() computes it (ItemDecl::
// wants_join, below) but does not act on it, and the actual grouping runs
// inside build_tree, per frame, over that frame's own children in push
// order — before pending-member injection and the placement-key sort,
// which is where it must be evaluated. ir::SpecItem itself never gains a
// field for this (ir.hpp's contract is the serialized JSON, not additional
// C++ members excluded from json_descriptor<T>::members, but the flag has
// no reason to survive past this one clang-free translation unit, so there
// is nothing to gain by putting it there and coordinating a schema-owner
// sign-off for it). group_items remains a small, independently unit-tested
// function, but its signature is vector<GroupCandidate> -> vector<GroupCandidate>, a
// flat run of *one frame's own pre-sort children*, because that is the
// only shape that can carry the join flag, named targets, and the push-order
// relation they need. build_tree is what supplies that
// shape and consumes the result; nothing outside this header calls
// group_items on anything but a single frame's pushed children.
//
// DocEvent names no clang:: type, so stages 2 and 3 live in this core,
// clang-free translation unit and carry tests that do not need
// a Clang parse (tests/beman/specgen/document_build.test.cpp) — frontend.cpp
// is Tier B and needs the Clang front end to build.
#ifndef BEMAN_SPECGEN_DOCUMENT_BUILD_HPP
#define BEMAN_SPECGEN_DOCUMENT_BUILD_HPP

#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/ir.hpp>

#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace beman::specgen::document_build {

// A non-fatal finding surfaced while building the document tree. These
// kinds reach this channel:
//
//  - A malformed `\rSec` marker: the tag itself matched but the grammar after
//    it (depth, `[stable]`, `{title}`) failed to parse. frontend.hpp's
//    parse_rsec doc comment explains the position-based distinction between
//    this case and a genuine non-match (a `\ref` group header, license/SPDX
//    text, a trailing-brace comment), which stays silent. The message is
//    parse_rsec's positioned parse_error, already rendered to text, and
//    carries the failure's offset *within the comment* in its own tail.
//  - A numbered draft-style heading ending in `[stable.name]` that is not a
//    `\rSec` marker. It cannot safely open a frame without a depth, but
//    warning prevents following declarations from being silently mistaken for
//    children of the previous section.
//  - Every grammar::Diagnostic a decl's `//!` docblock produced: the
//    element-ordering Note, the duplicate-element and marker Warnings, and
//    the unknown-tag Error. grammar::parse_docblock computes these; carrying
//    them here is what lets a `\remarks` written before `\effects` — or a
//    misspelled tag whose whole element is silently discarded — reach the
//    reader instead of no one.
//
// `line` is 1-based in the main file, which is what the driver prints
// and therefore what the reader has to be able to find — a raw byte offset
// would read, as `header:4127:` does to a reader, like a line number that
// does not exist. A docblock
// diagnostic's own line is 1-based *within the docblock*, so classify() adds
// it to the line the docblock starts on.
//
// `severity` is grammar::Diagnostic's own, carried through rather than
// flattened to a blanket `warning:` — true of a malformed
// `\rSec` and false of an unknown tag.
struct Diagnostic {
    beman::specgen::Severity severity = beman::specgen::Severity::Warning;
    unsigned                 line     = 0;
    std::string              message;
};

// Opens a nested ir::Section frame (design §3.2): a `\rSec<depth>[stable]
// {title}` marker, already parsed by classify()'s call to frontend::parse_rsec.
// build_tree closes every open frame at depth >= this one before opening its
// own — a `\rSec1` after two nested `\rSec3`/`\rSec2` frames closes both.
struct SectionOpen {
    unsigned    offset = 0; // this frame's own placement key in its parent, once closed
    int         depth  = 0;
    std::string stable;
    std::string title;
};

// An in-class-defined member's SpecItem awaiting placement (design
// §3.3): harvested from a class body by classify() (which has the
// clang::CXXRecordDecl in hand while building the class's own SynopsisDecl),
// tagged with the stable name of the `\rSec` section its `\ref` group names
// and its class-body offset (the placement key that interleaves it with
// out-of-line siblings). build_tree injects it as a child of that section
// when the section closes, after the front end has already grouped adjacent
// documented aliases and after that section's own out-of-line
// `\also`/empty-descr grouping has run. build_tree itself never subjects an
// injected in-class item to another join check;
// a stable name that names no section is silently dropped (a design §9
// coverage concern).
struct PendingItem {
    std::string                  stable;
    unsigned                     offset = 0;
    beman::specgen::ir::SpecItem item;
    bool                         is_alias   = false; // transient alias-grouping metadata
    bool                         wants_join = false; // adjacent alias carries \also
};

// A class/struct/union (or class template) top-level decl (design §3.4): its
// synopsis, plus any in-class-defined members classify() collected from its
// body alongside it (the same class-body walk serves both). A
// forward declaration carries an empty Synopsis and no pending items. The
// event itself is never a join candidate; adjacent alias joining is completed
// inside its pending list before build_tree sees the event.
//
// `diagnostics` are the docblock findings of the *members* collected
// alongside the synopsis — an in-class member's markup is parsed while
// walking the class body, so its findings have nowhere else to ride. They sit
// on the SynopsisDecl rather than on each PendingItem because a pending item
// routed to a stable name no `\rSec` opens is dropped (design §3.3), and a
// misspelled tag is worth reporting whether or not the wording it belongs to
// found a home.
struct SynopsisDecl {
    unsigned                     offset = 0;
    beman::specgen::ir::Synopsis synopsis;
    std::vector<PendingItem>     pending;
    std::vector<Diagnostic>      diagnostics = {};
    // Direct class-scope static_asserts (design §5.2) become one
    // general-subclause paragraph adjacent to the class synopsis. This is a
    // sibling IR node rather than synopsis metadata, so every backend renders
    // it through the existing FreeParagraph case.
    std::optional<beman::specgen::ir::FreeParagraph> general = {};
};

// An out-of-line function definition's SpecItem (design §3.3), keyed by its
// in-class *declaration* offset (the placement key, so it interleaves
// with in-class members in class-body order rather than definition order —
// see this header's top-of-file note on why that key can, and does, differ
// from push order).
//
// `wants_join` is classify()'s half of `\also`/empty-descr grouping (design
// §4.3): "does this item want to attach to whatever precedes it" —
// `\also`-marked, or carrying no description at all — is a property of this
// item alone, computed once here from directives that never leave this
// event. Whether a join actually *happens* depends on tree-adjacency
// context classify() does not have (is there in fact a preceding primary in
// the same open frame, in push order); that half is build_tree's, decided
// while `item` is unmodified — see the header's top-of-file note. A
// follower whose join never fires keeps its own `item` exactly as written,
// content included.
//
// `diagnostics` are the findings this item's own `//!` docblock
// produced. They ride the event rather than the ir::SpecItem for the reason
// ir.hpp gives about `wants_join` above: the IR's contract is the serialized
// JSON, and a docblock finding has no reason to outlive the build. They are
// reported even when the item is later merged into a group primary — the
// follower's *content* is dropped by grouping, but the fact that its markup
// was malformed is not about where its content ended up.
//
// `group_id` and `also_target` carry the named-group directives. Like
// `wants_join`, they are transient: group_items consumes them within one
// frame before placement sorting, and none enters ir::SpecItem or JSON.
// `grouping_line` gives frame-dependent errors a source location.
struct ItemDecl {
    unsigned                     placement_key = 0;
    bool                         wants_join    = false;
    beman::specgen::ir::SpecItem item;
    std::vector<Diagnostic>      diagnostics   = {};
    std::optional<std::string>   group_id      = {};
    std::optional<std::string>   also_target   = {};
    unsigned                     grouping_line = 0;
};

// A raw item that classify() determined carries no structure of its own:
// a comment that is not a `\rSec` marker (a `\ref` group header, license/SPDX
// text, a trailing-brace comment), a malformed `\rSec`, or a top-level decl
// that yields no node at all (not a function definition, or carrying
// `\omit`/`\merge`). build_tree skips the node either way; whatever
// Diagnostics the item produced on its way to being ignored are the only part
// of it that survives, collected into BuildResult::diagnostics below.
//
// Two kinds of item reach this event carrying findings: a malformed `\rSec`
// (one Diagnostic) and an `\omit`/`\merge`d function definition (its
// docblock is parsed before the marker is read, so its grammar findings
// exist and are worth reporting; the entity is unspecified on purpose, but a
// misspelled tag in it is still a typo). That second case is why this is a
// vector rather than a single std::optional.
struct Ignored {
    std::vector<Diagnostic> diagnostics;
};

// classify()'s output (decision document-build-stages): a closed variant naming no
// clang:: type, which is what lets build_tree and group_items below be
// clang-free.
using DocEvent = std::variant<SectionOpen, SynopsisDecl, ItemDecl, Ignored>;

// One node awaiting placement in the frame currently being built, carrying
// exactly what group_items and the placement-key sort each need: `key` is
// the child's placement key (design §3.3), `node` is the child itself, and
// `wants_join`, `group_id`, and `also_target` are ItemDecl's transient
// grouping metadata (all empty/false for
// every non-ItemDecl child: a Synopsis, a folded-in nested Section, or an
// injected PendingItem are never join candidates). A merged group's
// surviving GroupCandidate keeps its primary's `key` — the primary keeps
// its placement key, and the follower's key is simply never
// added. Its ItemDecl keeps the stable exact-distinct union of both items'
// index metadata alongside their combined signatures.
struct GroupCandidate {
    unsigned                   key = 0;
    beman::specgen::ir::Node   node;
    bool                       wants_join    = false;
    std::optional<std::string> group_id      = {};
    std::optional<std::string> also_target   = {};
    unsigned                   grouping_line = 0;
};

// build_tree()'s return: the document,
// alongside every Diagnostic collected
// from the events along the way — Validation style (decision
// expected-error-taxonomy's fourth row: carry
// `{value, vector<Diagnostic>}` rather than fail fast), since neither a
// malformed `\rSec` nor a malformed docblock stops the build, they are just
// worth reporting once the build is done. Order is the order build_tree
// visits events in (source offset order, and within one SynopsisDecl its
// members' class-body order), not sorted or deduplicated — so the list reads
// down the header, which is what a caller printing it wants.
struct BuildResult {
    beman::specgen::ir::Document document;
    std::vector<Diagnostic>      diagnostics;
};

// Stage 2 (plus stage 3 folded in — see the top-of-file note): fold the
// flat, offset-ordered `events` into the `\rSec` section tree (design §3.2).
// For each open frame, in event order: push each child as a GroupCandidate
// (an ItemDecl's own `wants_join`, false for everything else); at a
// shallower-or-equal `\rSec` or at end of input, group_items() runs over
// *that frame's own pushed candidates, in push order* (`\also`/empty-descr
// grouping plus named grouping, design §4.3), *then* any pending
// in-class members routed to this
// frame's stable name are appended ungrouped, and only then is the whole
// list stable-sorted into placement-key order and folded into the parent as
// an ir::Section. The frame stack itself is the one marked substrate
// algorithm below (a fold carrying a frame stack, not unfold_with —
// decision document-build-stages): the events arrive linearly with explicit
// depths, so an unfold would need lookahead plumbing for no gain.
//
// Every event's Diagnostics — an Ignored's, an
// ItemDecl's and a SynopsisDecl's — are collected in visit order into
// the returned BuildResult::diagnostics. The tree itself is
// unaffected: a reported item is still built,
// placed and grouped as though nothing were said about it.
//
// Clang-free; unit-tested with synthetic events in
// tests/beman/specgen/document_build.test.cpp.
BuildResult build_tree(std::span<DocEvent> events);

// Merge a grouped follower's declarations into its primary. Signatures append
// in source order; index metadata forms a stable exact-value union so overloads
// do not repeat one entry and differently named aliases do not lose theirs.
void append_grouped_itemdecl(beman::specgen::ir::ItemDecl& primary, beman::specgen::ir::ItemDecl&& follower);

// Stage 3: `\also`/empty-descr grouping (design §4.3) over one frame's
// own children, in the push order build_tree accumulated them in — *not*
// `ir::Document -> ir::Document`; see this header's top-of-file note for
// why. The left-to-right fold preserves the existing adjacent relation for a
// `wants_join` candidate, and registers `group_id` primaries so a later
// `also_target` candidate can join one across intervening items.
// Named resolution is frame-local and cannot see forward. Merging appends the
// follower's itemdecl signatures onto the primary's ir::SpecItem and drops
// the follower (its own content, empty or not, is not part of the result).
//
// A follower's `\also` intent silently does *not* fire when nothing eligible
// precedes it (the frame's first candidate, or the previous candidate wraps
// a Synopsis/Section/FreeParagraph): that candidate is not merged with
// anything and stands alone, its `item` untouched —
// `wants_join` is read but never used to mutate
// `item` before the adjacency check runs.
// A duplicate group ID or unresolved named target appends an Error to
// `diagnostics` and leaves that candidate standalone and unmodified.
//
// Clang-free; unit-tested with synthetic candidate lists in
// tests/beman/specgen/document_build.test.cpp.
std::vector<GroupCandidate> group_items(std::vector<GroupCandidate> candidates,
                                        std::vector<Diagnostic>*    diagnostics = nullptr);

} // namespace beman::specgen::document_build

#endif // BEMAN_SPECGEN_DOCUMENT_BUILD_HPP
