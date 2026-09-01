// tests/beman/specgen/document_build.test.cpp                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// build_tree() and group_items() (decision document-build-stages) driven by
// synthetic DocEvent/GroupCandidate values — tests of the `\rSec`
// frame-stack fold and the `\also`/empty-descr grouping relation that do not
// need a Clang parse, since both are clang-free (frontend.cpp's classify()
// remains the only place that touches a clang::Decl*, and is exercised
// instead by the Tier B tests in tests/beman/specgen/frontend/).
//
// group_items() takes vector<GroupCandidate>, not ir::Document — see
// document_build.hpp's top-of-file note. Two cases here exist specifically
// to pin the two properties that note explains: an `\also` item with real
// content and no preceding primary must keep that content (not have it
// cleared just because it *asked* to join), and grouping must be decided in
// push order, before the placement-key sort — not after.

#include <beman/specgen/document_build.hpp>
#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <span>
#include <string>
#include <variant>
#include <vector>

namespace db = beman::specgen::document_build;
namespace ir = beman::specgen::ir;

using beman::specgen::Severity;

namespace {

ir::SpecItem make_item(std::string sig_text, bool with_descr) {
    ir::SpecItem item;
    item.decl.signatures.push_back(ir::CodeText{std::move(sig_text), {}});
    if (with_descr)
        item.descr.elements.push_back(ir::DescriptionElement{ir::ElementKind::Effects, {}, {}, {}});
    return item;
}

// Most of this file's cases only care about the tree, not the diagnostics
// channel build_tree returns alongside it — this helper
// keeps them all reading `build(...)` as an ir::Document.
// Diagnostics-specific cases below call db::build_tree directly instead.
ir::Document build(std::vector<db::DocEvent> events) { return db::build_tree(std::span(events)).document; }

} // namespace

// --- build_tree: frame-stack nesting/un-nesting -----------------------------

TEST_CASE("build_tree - flat items with no sections sort into placement-key order at the root") {
    // Placement keys deliberately out of push order: the root frame is
    // sorted too (design §3.3), not just left in source order. Neither item
    // wants to join, so the sort is the only thing reordering them.
    auto doc = build({
        db::ItemDecl{20, false, make_item("B();", true)},
        db::ItemDecl{10, false, make_item("A();", true)},
    });
    REQUIRE(doc.nodes.size() == 2);
    const auto* first  = std::get_if<ir::SpecItem>(&doc.nodes[0]);
    const auto* second = std::get_if<ir::SpecItem>(&doc.nodes[1]);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first->decl.signatures[0].text == "A();");
    CHECK(second->decl.signatures[0].text == "B();");
}

TEST_CASE("build_tree - a \\rSec nests a section and folds it in at EOF") {
    auto doc = build({
        db::SectionOpen{100, 1, "widget.cons", "Constructors"},
        db::ItemDecl{5, false, make_item("widget();", true)},
    });
    REQUIRE(doc.nodes.size() == 1);
    const auto* section = std::get_if<ir::Section>(&doc.nodes[0]);
    REQUIRE(section != nullptr);
    CHECK(section->stable_name == "widget.cons");
    CHECK(section->title == "Constructors");
    REQUIRE(section->children.size() == 1);
    CHECK(std::holds_alternative<ir::SpecItem>(section->children[0]));
}

TEST_CASE("build_tree - un-nesting: a shallower \\rSec closes only the frames at or below its depth") {
    // \rSec1 A, \rSec2 B nested in A, \rSec2 D — a second depth-2 section —
    // closes only B (depth 2 >= 2), leaving A open, so B and D land as
    // siblings under A rather than D nesting inside B.
    auto doc = build({
        db::SectionOpen{0, 1, "a", "A"},
        db::SectionOpen{1, 2, "a.b", "B"},
        db::SectionOpen{2, 2, "a.d", "D"},
    });
    REQUIRE(doc.nodes.size() == 1);
    const auto* a = std::get_if<ir::Section>(&doc.nodes[0]);
    REQUIRE(a != nullptr);
    CHECK(a->stable_name == "a");
    REQUIRE(a->children.size() == 2);
    const auto* b = std::get_if<ir::Section>(&a->children[0]);
    const auto* d = std::get_if<ir::Section>(&a->children[1]);
    REQUIRE(b != nullptr);
    REQUIRE(d != nullptr);
    CHECK(b->stable_name == "a.b");
    CHECK(d->stable_name == "a.d");
}

TEST_CASE("build_tree - a depth jump closes several frames at once") {
    // \rSec1 A, \rSec2 B nested in A, \rSec3 C nested in B, an item in C,
    // then \rSec1 D: closes C (3>=1), then B (2>=1), then A (1>=1) in one
    // step, so D opens as A's sibling at the root rather than nesting inside
    // any of them.
    auto doc = build({
        db::SectionOpen{0, 1, "a", "A"},
        db::SectionOpen{1, 2, "a.b", "B"},
        db::SectionOpen{2, 3, "a.b.c", "C"},
        db::ItemDecl{3, false, make_item("f();", true)},
        db::SectionOpen{4, 1, "d", "D"},
    });
    REQUIRE(doc.nodes.size() == 2);
    const auto* a = std::get_if<ir::Section>(&doc.nodes[0]);
    const auto* d = std::get_if<ir::Section>(&doc.nodes[1]);
    REQUIRE(a != nullptr);
    REQUIRE(d != nullptr);
    CHECK(a->stable_name == "a");
    CHECK(d->stable_name == "d");
    REQUIRE(a->children.size() == 1);
    const auto* b = std::get_if<ir::Section>(&a->children[0]);
    REQUIRE(b != nullptr);
    REQUIRE(b->children.size() == 1);
    const auto* c = std::get_if<ir::Section>(&b->children[0]);
    REQUIRE(c != nullptr);
    CHECK(c->stable_name == "a.b.c");
    REQUIRE(c->children.size() == 1);
    CHECK(std::holds_alternative<ir::SpecItem>(c->children[0]));
    CHECK(d->children.empty());
}

TEST_CASE("build_tree - Ignored events contribute no node") {
    auto doc = build({
        db::Ignored{},
        db::ItemDecl{1, false, make_item("f();", true)},
        db::Ignored{},
    });
    REQUIRE(doc.nodes.size() == 1);
}

TEST_CASE("build_tree - a class-general paragraph stays adjacent to its synopsis in the active section") {
    db::SynopsisDecl synopsis;
    synopsis.offset        = 10;
    synopsis.synopsis.code = ir::CodeText{"template <class T> class widget;", {}};
    synopsis.general       = ir::FreeParagraph{{ir::TextInline{"Class-general wording."}}};

    auto doc = build({
        db::SectionOpen{0, 1, "widget.general", "General"},
        std::move(synopsis),
        db::ItemDecl{20, false, make_item("f();", true)},
    });

    REQUIRE(doc.nodes.size() == 1);
    const auto* section = std::get_if<ir::Section>(&doc.nodes[0]);
    REQUIRE(section != nullptr);
    REQUIRE(section->children.size() == 3);
    CHECK(std::holds_alternative<ir::Synopsis>(section->children[0]));
    CHECK(std::holds_alternative<ir::FreeParagraph>(section->children[1]));
    CHECK(std::holds_alternative<ir::SpecItem>(section->children[2]));
}

TEST_CASE("build_tree - a pending in-class item is injected into the section its \\ref group names, "
          "sorted against the section's own children by placement key") {
    // The class's own SynopsisDecl (and the pending member it harvested) is
    // seen before the \rSec section it targets even opens — collect_
    // inclass_items routes by stable name, not by tree position — and the
    // pending item's placement key (2) sorts it ahead of the section's own
    // out-of-line item (key 5). Neither is a join candidate: a pending item
    // is never subject to the join check (see PendingItem's doc comment).
    db::PendingItem  pending{"widget.cons", 2, make_item("widget(int);", true)};
    db::SynopsisDecl synopsis{
        0, ir::Synopsis{.name = {}, .code = ir::CodeText{"class widget { ... };", {}}, .roster = {}}, {pending}};

    auto doc = build({
        std::move(synopsis),
        db::SectionOpen{10, 1, "widget.cons", "Constructors"},
        db::ItemDecl{5, false, make_item("widget();", true)},
    });

    REQUIRE(doc.nodes.size() == 2);
    CHECK(std::holds_alternative<ir::Synopsis>(doc.nodes[0]));
    const auto* section = std::get_if<ir::Section>(&doc.nodes[1]);
    REQUIRE(section != nullptr);
    CHECK(section->stable_name == "widget.cons");
    REQUIRE(section->children.size() == 2);
    const auto* first  = std::get_if<ir::SpecItem>(&section->children[0]);
    const auto* second = std::get_if<ir::SpecItem>(&section->children[1]);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first->decl.signatures[0].text == "widget(int);"); // key 2
    CHECK(second->decl.signatures[0].text == "widget();");   // key 5
}

TEST_CASE("build_tree - grouping applies independently inside a nested Section") {
    // A primary/follower pair inside a \rSec, proving stage 3 runs per frame
    // (at that frame's own close) rather than needing a separate recursive
    // pass over the finished tree.
    auto doc = build({
        db::SectionOpen{0, 1, "span.access", "Access"},
        db::ItemDecl{1, false, make_item("front();", true)},
        db::ItemDecl{2, true, make_item("front() const;", false)},
    });
    REQUIRE(doc.nodes.size() == 1);
    const auto* section = std::get_if<ir::Section>(&doc.nodes[0]);
    REQUIRE(section != nullptr);
    REQUIRE(section->children.size() == 1);
    const auto* merged = std::get_if<ir::SpecItem>(&section->children[0]);
    REQUIRE(merged != nullptr);
    CHECK(merged->decl.signatures.size() == 2);
}

// --- Regression: content preservation and push-order grouping ---------------

TEST_CASE("build_tree - an \\also item with content and no preceding primary keeps its content") {
    // A follower's own descr must survive when no primary precedes it -- it
    // must not be cleared just because the item *asked* to join.
    auto doc = build({
        db::ItemDecl{1, /*wants_join=*/true, make_item("scratch();", /*with_descr=*/true)},
    });
    REQUIRE(doc.nodes.size() == 1);
    const auto* item = std::get_if<ir::SpecItem>(&doc.nodes[0]);
    REQUIRE(item != nullptr);
    REQUIRE(item->decl.signatures.size() == 1);
    REQUIRE(item->descr.elements.size() == 1); // content preserved, not dropped
}

TEST_CASE("build_tree - grouping is decided in push order, before the placement-key sort") {
    // The primary is pushed first with the *larger* placement
    // key (200 -- e.g. declared later in the class body than the follower);
    // the follower is pushed immediately after it (key 100) and wants to
    // join. Grouping is decided against push-order
    // adjacency, merging the two before the sort ever reorders them by key.
    // A post-sort grouping pass would instead see key-100 first with nothing
    // before it, and key-200 second not wanting to join, producing two
    // ungrouped items -- exactly the bug this test pins against regressing.
    auto doc = build({
        db::ItemDecl{200, false, make_item("primary();", true)},
        db::ItemDecl{100, true, make_item("follower();", false)},
    });
    REQUIRE(doc.nodes.size() == 1);
    const auto* merged = std::get_if<ir::SpecItem>(&doc.nodes[0]);
    REQUIRE(merged != nullptr);
    REQUIRE(merged->decl.signatures.size() == 2);
    CHECK(merged->decl.signatures[0].text == "primary();");
    CHECK(merged->decl.signatures[1].text == "follower();");
    REQUIRE(merged->descr.elements.size() == 1); // the primary's own descr, untouched
}

TEST_CASE("build_tree - push-order grouping also holds inside a section frame") {
    // The same push-order guarantee as the test above, but for a frame
    // closed by close_top() rather than for the root. build_tree groups at
    // two separate call sites -- once per closed frame, once for the root --
    // and only the root one is pinned above, so moving just this one after
    // sort_frame would otherwise go unnoticed.
    auto doc = build({
        db::SectionOpen{10, 1, "widget.cons", "Constructors"},
        db::ItemDecl{200, false, make_item("primary();", true)},
        db::ItemDecl{100, true, make_item("follower();", false)},
    });
    REQUIRE(doc.nodes.size() == 1);
    const auto* section = std::get_if<ir::Section>(&doc.nodes[0]);
    REQUIRE(section != nullptr);
    REQUIRE(section->children.size() == 1);
    const auto* merged = std::get_if<ir::SpecItem>(&section->children[0]);
    REQUIRE(merged != nullptr);
    REQUIRE(merged->decl.signatures.size() == 2);
    CHECK(merged->decl.signatures[0].text == "primary();");
    CHECK(merged->decl.signatures[1].text == "follower();");
}

// --- group_items: the \\also/empty-descr grouping relation ------------------
//
// group_items() operates on one frame's own children, in push order, each
// tagged with whether it wants to join (see document_build.hpp) -- not on an
// ir::Document. These tests drive it directly with synthetic candidate lists.

TEST_CASE("group_items - a wants_join candidate joins the SpecItem immediately before it") {
    std::vector<db::GroupCandidate> candidates;
    candidates.push_back(db::GroupCandidate{0, make_item("front();", true), false});
    candidates.push_back(db::GroupCandidate{1, make_item("front() const;", false), true});

    auto grouped = db::group_items(std::move(candidates));

    REQUIRE(grouped.size() == 1);
    CHECK(grouped[0].key == 0); // the primary's key survives
    const auto* merged = std::get_if<ir::SpecItem>(&grouped[0].node);
    REQUIRE(merged != nullptr);
    REQUIRE(merged->decl.signatures.size() == 2);
    CHECK(merged->decl.signatures[0].text == "front();");
    CHECK(merged->decl.signatures[1].text == "front() const;");
    REQUIRE(merged->descr.elements.size() == 1); // the primary's own descr, untouched
}

TEST_CASE("group_items - a run of followers all join the same primary") {
    std::vector<db::GroupCandidate> candidates;
    candidates.push_back(db::GroupCandidate{0, make_item("f(int);", true), false});
    candidates.push_back(db::GroupCandidate{1, make_item("f(long);", false), true});
    candidates.push_back(db::GroupCandidate{2, make_item("f(double);", false), true});

    auto grouped = db::group_items(std::move(candidates));

    REQUIRE(grouped.size() == 1);
    const auto* merged = std::get_if<ir::SpecItem>(&grouped[0].node);
    REQUIRE(merged != nullptr);
    REQUIRE(merged->decl.signatures.size() == 3);
}

TEST_CASE("group_items - repeated overload indexes are deduplicated exactly") {
    auto primary  = make_item("value() &;", true);
    auto follower = make_item("value() const &;", false);
    primary.decl.index.push_back({ir::IndexKind::Member, "value", "optional"});
    follower.decl.index.push_back({ir::IndexKind::Member, "value", "optional"});

    std::vector<db::GroupCandidate> candidates;
    candidates.push_back(db::GroupCandidate{0, std::move(primary), false});
    candidates.push_back(db::GroupCandidate{1, std::move(follower), true});

    auto grouped = db::group_items(std::move(candidates));

    REQUIRE(grouped.size() == 1);
    const auto& merged = std::get<ir::SpecItem>(grouped[0].node);
    REQUIRE(merged.decl.signatures.size() == 2);
    REQUIRE(merged.decl.index.size() == 1);
    CHECK(merged.decl.index[0].kind == ir::IndexKind::Member);
    CHECK(merged.decl.index[0].name == "value");
    CHECK(merged.decl.index[0].parent == "optional");
}

TEST_CASE("group_items - distinct alias indexes survive grouping in order") {
    auto primary  = make_item("using value_type = T;", true);
    auto follower = make_item("using reference = T&;", false);
    primary.decl.index.push_back({ir::IndexKind::Member, "value_type", "optional"});
    follower.decl.index.push_back({ir::IndexKind::Member, "reference", "optional"});

    std::vector<db::GroupCandidate> candidates;
    candidates.push_back(db::GroupCandidate{0, std::move(primary), false});
    candidates.push_back(db::GroupCandidate{1, std::move(follower), true});

    auto grouped = db::group_items(std::move(candidates));

    REQUIRE(grouped.size() == 1);
    const auto& merged = std::get<ir::SpecItem>(grouped[0].node);
    REQUIRE(merged.decl.signatures.size() == 2);
    REQUIRE(merged.decl.index.size() == 2);
    CHECK(merged.decl.index[0].name == "value_type");
    CHECK(merged.decl.index[1].name == "reference");
}

TEST_CASE("group_items - a wants_join candidate with nothing before it stays standalone") {
    std::vector<db::GroupCandidate> candidates;
    candidates.push_back(db::GroupCandidate{0, make_item("scratch();", true), true}); // no primary to join

    auto grouped = db::group_items(std::move(candidates));

    REQUIRE(grouped.size() == 1);
    const auto* item = std::get_if<ir::SpecItem>(&grouped[0].node);
    REQUIRE(item != nullptr);
    REQUIRE(item->decl.signatures.size() == 1);
    REQUIRE(item->descr.elements.size() == 1); // content kept: it never found a primary
}

TEST_CASE("group_items - a wants_join candidate does not join a non-SpecItem predecessor") {
    std::vector<db::GroupCandidate> candidates;
    candidates.push_back(db::GroupCandidate{
        0, ir::Synopsis{.name = {}, .code = ir::CodeText{"class widget { ... };", {}}, .roster = {}}, false});
    candidates.push_back(db::GroupCandidate{1, make_item("widget();", false), true});

    auto grouped = db::group_items(std::move(candidates));

    REQUIRE(grouped.size() == 2);
    CHECK(std::holds_alternative<ir::Synopsis>(grouped[0].node));
    const auto* item = std::get_if<ir::SpecItem>(&grouped[1].node);
    REQUIRE(item != nullptr);
    REQUIRE(item->decl.signatures.size() == 1);
}

TEST_CASE("group_items - two candidates that both decline to join are left ungrouped") {
    std::vector<db::GroupCandidate> candidates;
    candidates.push_back(db::GroupCandidate{0, make_item("size() const;", true), false});
    candidates.push_back(db::GroupCandidate{1, make_item("empty() const;", true), false}); // unrelated, own descr

    auto grouped = db::group_items(std::move(candidates));

    REQUIRE(grouped.size() == 2);
    for (const db::GroupCandidate& c : grouped) {
        const auto* item = std::get_if<ir::SpecItem>(&c.node);
        REQUIRE(item != nullptr);
        REQUIRE(item->decl.signatures.size() == 1);
        REQUIRE(item->descr.elements.size() == 1);
    }
}

TEST_CASE("group_items - named followers join interleaved preceding primaries") {
    std::vector<db::GroupCandidate> candidates{
        {10, make_item("f() &;", true), false, "left"},
        {20, make_item("f() &&;", true), false, "right"},
        {30, make_item("f() const &;", false), false, std::nullopt, "left"},
        {40, make_item("f() const &&;", false), false, std::nullopt, "right"},
    };

    auto grouped = db::group_items(std::move(candidates));

    REQUIRE(grouped.size() == 2);
    const auto& left  = std::get<ir::SpecItem>(grouped[0].node);
    const auto& right = std::get<ir::SpecItem>(grouped[1].node);
    REQUIRE(left.decl.signatures.size() == 2);
    REQUIRE(right.decl.signatures.size() == 2);
    CHECK(left.decl.signatures[1].text == "f() const &;");
    CHECK(right.decl.signatures[1].text == "f() const &&;");
}

TEST_CASE("group_items - duplicate and unresolved names are errors and retain their items") {
    std::vector<db::GroupCandidate> candidates{
        {10, make_item("first();", true), false, "same", std::nullopt, 7},
        {20, make_item("duplicate();", true), false, "same", std::nullopt, 9},
        {30, make_item("forward();", false), false, std::nullopt, "later", 11},
        {40, make_item("later();", true), false, "later", std::nullopt, 13},
        {50, make_item("missing();", false), false, std::nullopt, "absent", 15},
    };
    std::vector<db::Diagnostic> diagnostics;

    auto grouped = db::group_items(std::move(candidates), &diagnostics);

    CHECK(grouped.size() == 5);
    REQUIRE(diagnostics.size() == 3);
    CHECK(diagnostics[0].line == 9);
    CHECK(diagnostics[0].message == "duplicate \\group id 'same' in this section");
    CHECK(diagnostics[1].line == 11);
    CHECK(diagnostics[1].message == "\\also target 'later' has no preceding \\group primary in this section");
    CHECK(diagnostics[2].line == 15);
    CHECK(diagnostics[2].message == "\\also target 'absent' has no preceding \\group primary in this section");
}

TEST_CASE("build_tree - named group targets are local to one section frame") {
    std::vector<db::DocEvent> events{
        db::SectionOpen{0, 1, "one", "One"},
        db::ItemDecl{10, false, make_item("primary();", true), {}, "shared", std::nullopt, 4},
        db::SectionOpen{20, 1, "two", "Two"},
        db::ItemDecl{30, false, make_item("follower();", false), {}, std::nullopt, "shared", 8},
    };

    auto result = db::build_tree(std::span(events));

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics[0].line == 8);
    REQUIRE(result.document.nodes.size() == 2);
    const auto& second = std::get<ir::Section>(result.document.nodes[1]);
    REQUIRE(second.children.size() == 1);
    CHECK(std::get<ir::SpecItem>(second.children[0]).decl.signatures[0].text == "follower();");
}

// --- build_tree: the diagnostics channel ------------------------------------
//
// classify() is the one that decides which findings an event carries
// (frontend.cpp, exercised end to end in
// tests/beman/specgen/frontend/diagnostics.test.cpp); these cases only pin
// that build_tree collects whatever an event carries, leaves the tree itself
// untouched either way, and does not invent one for an event that carries
// none.

TEST_CASE("build_tree - an Ignored event with no diagnostic contributes neither a node nor a diagnostic") {
    std::vector<db::DocEvent> events{db::Ignored{}};
    auto                      result = db::build_tree(std::span(events));
    CHECK(result.document.nodes.empty());
    CHECK(result.diagnostics.empty());
}

TEST_CASE("build_tree - an Ignored event's Diagnostic is collected without perturbing the tree") {
    std::vector<db::DocEvent> events{
        db::Ignored{{db::Diagnostic{Severity::Warning, 7, "malformed \\rSec marker: expected ']'"}}},
        db::ItemDecl{1, false, make_item("f();", true)},
    };
    auto result = db::build_tree(std::span(events));

    REQUIRE(result.document.nodes.size() == 1); // the Ignored event contributes no node
    CHECK(std::holds_alternative<ir::SpecItem>(result.document.nodes[0]));

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics[0].line == 7);
    CHECK(result.diagnostics[0].severity == Severity::Warning);
    CHECK(result.diagnostics[0].message == "malformed \\rSec marker: expected ']'");
}

TEST_CASE("build_tree - diagnostics from inside a section frame surface alongside diagnostics at the root") {
    std::vector<db::DocEvent> events{
        db::Ignored{{db::Diagnostic{Severity::Warning, 1, "root diagnostic"}}},
        db::SectionOpen{2, 1, "widget.cons", "Constructors"},
        db::Ignored{{db::Diagnostic{Severity::Warning, 3, "nested diagnostic"}}},
        db::ItemDecl{4, false, make_item("widget();", true)},
    };
    auto result = db::build_tree(std::span(events));

    REQUIRE(result.diagnostics.size() == 2);
    CHECK(result.diagnostics[0].line == 1);
    CHECK(result.diagnostics[1].line == 3);
}

// --- the docblock findings the other two event kinds carry ------------------
//
// An item's own markup findings ride its ItemDecl or SynopsisDecl, not just
// Ignored. These pin those two channels,
// and — the part that is easy to get wrong — that a finding
// survives the two transformations build_tree applies to its item.

TEST_CASE("build_tree - an ItemDecl's docblock diagnostics are collected") {
    std::vector<db::DocEvent> events{
        db::ItemDecl{
            1,
            false,
            make_item("f();", true),
            {db::Diagnostic{Severity::Note, 12, "\\effects appears after \\remarks; output is canonicalized"}}},
    };
    auto result = db::build_tree(std::span(events));

    REQUIRE(result.document.nodes.size() == 1); // the item is built exactly as before
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics[0].severity == Severity::Note);
    CHECK(result.diagnostics[0].line == 12);
}

TEST_CASE("build_tree - a group follower's diagnostics survive being merged into its primary") {
    // The follower's *content* is dropped by \also grouping (design §4.3);
    // the fact that its markup was malformed is a separate claim and must
    // not be dropped with it.
    std::vector<db::DocEvent> events{
        db::ItemDecl{1, false, make_item("f(int);", true)},
        db::ItemDecl{
            2, true, make_item("f(long);", true), {db::Diagnostic{Severity::Error, 20, "unknown tag \\effect"}}},
    };
    auto result = db::build_tree(std::span(events));

    REQUIRE(result.document.nodes.size() == 1); // merged into the primary
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics[0].severity == Severity::Error);
    CHECK(result.diagnostics[0].message == "unknown tag \\effect");
}

TEST_CASE("build_tree - a SynopsisDecl's member diagnostics survive a pending item routed nowhere") {
    // A pending member whose stable name names no \rSec section is dropped
    // (design §3.3). Its class's diagnostics are collected when the
    // SynopsisDecl is visited, so the drop cannot take them with it — which
    // is why they ride the SynopsisDecl rather than the PendingItem.
    db::SynopsisDecl syn;
    syn.offset      = 1;
    syn.pending     = {db::PendingItem{"no.such.section", 2, ir::SpecItem{}}};
    syn.diagnostics = {db::Diagnostic{Severity::Error, 5, "unknown tag \\effect"}};

    std::vector<db::DocEvent> events{std::move(syn)};
    auto                      result = db::build_tree(std::span(events));

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics[0].message == "unknown tag \\effect");
}
