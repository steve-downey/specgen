// tests/beman/specgen/validate/validate.test.cpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The validator skeleton's core coverage: the diagnostics monoid's laws (in
// the style of foundation/monoid.test.cpp), a clean document folding to the
// identity, each of the CodeText::spans validator's three malformed
// kinds, context prefixing composing through two levels of section nesting
// -- the case a single-level test would miss -- and has_errors's severity
// gate.

#include <beman/specgen/validate/validate.hpp>
#include <beman/specgen/validate/validate.hpp> // Re-inclusion verification

#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace beman::specgen::validate;
// Severity lives one namespace up (diagnostic.hpp); the using-directive above
// only imports names declared directly in beman::specgen::validate, so it is
// named explicitly here.
using beman::specgen::Severity;
namespace ir = beman::specgen::ir;

TEST_CASE("validate - HeaderIsIdempotent") {
    // Placeholder: verifies header re-inclusion safety and build coherency.
    REQUIRE(true);
}

// --- diagnostics_monoid laws (mirrors foundation/monoid.test.cpp) ----------

TEST_CASE("validate - diagnostics_monoid identity law: combine(identity, a) == a == combine(a, identity)") {
    const Diagnostics a{
        {Severity::Error, "ctx", "one"},
        {Severity::Warning, "ctx", "two"},
    };
    CHECK(diagnostics_monoid.combine(diagnostics_monoid.identity, a) == a);
    CHECK(diagnostics_monoid.combine(a, diagnostics_monoid.identity) == a);
}

TEST_CASE("validate - diagnostics_monoid associativity law: combine(a, combine(b, c)) == combine(combine(a, b), c)") {
    const Diagnostics a{{Severity::Error, "a", "1"}};
    const Diagnostics b{{Severity::Warning, "b", "2"}};
    const Diagnostics c{{Severity::Note, "c", "3"}};
    CHECK(diagnostics_monoid.combine(a, diagnostics_monoid.combine(b, c)) ==
          diagnostics_monoid.combine(diagnostics_monoid.combine(a, b), c));
}

// --- a clean document ------------------------------------------------------

TEST_CASE("validate - a clean document folds to the identity") {
    ir::Document doc;
    doc.nodes.push_back(ir::Node{ir::Synopsis{.name = {}, .code = {"void f();", {}}, .roster = {}}});
    doc.nodes.push_back(ir::Node{ir::FreeParagraph{{ir::TextInline{"General."}}}});
    CHECK(validate(doc).empty());
}

TEST_CASE("validate - a Document is a forest: findings from every root, in root order") {
    // ir::Document has no NodeF of its own (ir_fold.hpp), so validate(Document)
    // folds each root separately and mconcats. That combine step is only
    // exercised here: with one clean root the identity hides whether the
    // second root's findings survive, and in which order.
    ir::SpecItem bad;
    bad.decl.signatures.push_back(ir::CodeText{"ab", {{5, 1, ir::SpanKind::ExposId, "x"}}}); // inverted

    ir::Document doc;
    doc.nodes.push_back(ir::Node{ir::Section{"first", "First", {ir::Node{bad}}}});
    doc.nodes.push_back(ir::Node{ir::Synopsis{
        .name = {}, .code = {"abc", {{0, 9, ir::SpanKind::ExposId, "y"}}}, .roster = {}}}); // out of range

    const Diagnostics diags = validate(doc);
    REQUIRE(diags.size() == 2);
    CHECK(diags[0].context == "first/itemdecl[0]");
    CHECK(diags[1].context == "synopsis");
}

TEST_CASE("validate - a FreeParagraph is the monoid identity") {
    ir::Node node = ir::FreeParagraph{{ir::TextInline{"hello"}}};
    CHECK(validate(node).empty());
}

TEST_CASE("validate - an empty synopsis is reported at Severity::Error with its context") {
    ir::Node          node  = ir::Synopsis{.name = "widget", .code = {"", {}}, .roster = {}};
    const Diagnostics diags = validate(node);
    REQUIRE(diags.size() == 1);
    CHECK(diags.front().severity == Severity::Error);
    CHECK(diags.front().context == "widget/synopsis");
    CHECK(diags.front().message.find("empty code block") != std::string::npos);
}

// --- each malformed-span kind ----------------------------------------------

TEST_CASE("validate - an inverted span is reported at Severity::Error with its context") {
    ir::Node node = ir::Synopsis{.name = {}, .code = {"abcdef", {{10, 2, ir::SpanKind::ExposId, "x"}}}, .roster = {}};
    const Diagnostics diags = validate(node);
    REQUIRE(diags.size() == 1);
    CHECK(diags.front().severity == Severity::Error);
    CHECK(diags.front().context == "synopsis");
    CHECK(diags.front().message.find("inverted") != std::string::npos);
}

TEST_CASE("validate - an out-of-range span is reported at Severity::Error with its context") {
    ir::Node node = ir::Synopsis{.name = {}, .code = {"abc", {{0, 10, ir::SpanKind::ExposId, "x"}}}, .roster = {}};
    const Diagnostics diags = validate(node);
    REQUIRE(diags.size() == 1);
    CHECK(diags.front().severity == Severity::Error);
    CHECK(diags.front().context == "synopsis");
    CHECK(diags.front().message.find("exceeds") != std::string::npos);
}

TEST_CASE("validate - an overlapping span pair is reported at Severity::Error with its context") {
    ir::Node node =
        ir::Synopsis{.name = {},
                     .code = {"abcdefghij", {{0, 5, ir::SpanKind::ExposId, "a"}, {3, 6, ir::SpanKind::ExposId, "b"}}},
                     .roster = {}};
    const Diagnostics diags = validate(node);
    REQUIRE(diags.size() == 1);
    CHECK(diags.front().severity == Severity::Error);
    CHECK(diags.front().context == "synopsis");
    CHECK(diags.front().message.find("before span 0") != std::string::npos);
}

TEST_CASE("validate - a well-formed span table produces no findings") {
    ir::Node node = ir::Synopsis{
        .code = {"abcdef", {{0, 2, ir::SpanKind::ExposId, "a"}, {2, 4, ir::SpanKind::ExposId, "b"}}}, .roster = {}};
    CHECK(validate(node).empty());
}

TEST_CASE("validate - SpecItem checks every signature, itemdecl[i] naming the offending one") {
    ir::SpecItem item;
    item.decl.signatures.push_back(ir::CodeText{"abc", {}});
    item.decl.signatures.push_back(ir::CodeText{"abc", {{2, 1, ir::SpanKind::ExposId, "x"}}}); // inverted
    ir::Node node = item;

    const Diagnostics diags = validate(node);
    REQUIRE(diags.size() == 1);
    CHECK(diags.front().context == "itemdecl[1]");
}

// --- context prefixing composes through nesting -----------------------------

TEST_CASE("validate - context prefixing composes through two levels of section nesting") {
    ir::SpecItem bad;
    bad.decl.signatures.push_back(ir::CodeText{"ab", {{5, 1, ir::SpanKind::ExposId, "x"}}}); // inverted

    ir::Node inner_section = ir::Section{"inner", "Inner", {ir::Node{bad}}};
    ir::Node outer_section = ir::Section{"outer", "Outer", {inner_section}};

    const Diagnostics diags = validate(outer_section);
    REQUIRE(diags.size() == 1);
    CHECK(diags.front().context == "outer/inner/itemdecl[0]");
}

TEST_CASE("validate - an empty stable_name contributes no context segment") {
    ir::SpecItem bad;
    bad.decl.signatures.push_back(ir::CodeText{"ab", {{5, 1, ir::SpanKind::ExposId, "x"}}}); // inverted

    ir::Node section = ir::Section{"", "Untitled", {ir::Node{bad}}};

    const Diagnostics diags = validate(section);
    REQUIRE(diags.size() == 1);
    CHECK(diags.front().context == "itemdecl[0]");
}

// --- the coverage invariant (design §9) -------------------------------------

namespace {

// A synopsis carrying `roster`, wrapped so a test reads as one expression.
ir::Node synopsis_with(std::string name, std::vector<ir::SynopsisEntry> roster) {
    return ir::Synopsis{.name = std::move(name), .code = {"class widget { ... };", {}}, .roster = std::move(roster)};
}

} // namespace

TEST_CASE("validate - every accounted-for disposition is silent") {
    // The six ways design §9 permits a class-body declaration to be accounted
    // for. None is a finding; if one of them ever becomes one, this fails
    // rather than the change passing unnoticed.
    const ir::Node node = synopsis_with("widget",
                                        {
                                            {"widget", ir::Disposition::Described, ""},
                                            {"empty", ir::Disposition::Routed, "widget.obs"},
                                            {"clone", ir::Disposition::Merged, ""},
                                            {"scratch", ir::Disposition::Omitted, ""},
                                            {"operator=", ir::Disposition::Defaulted, ""},
                                            {"value_", ir::Disposition::Expos, ""},
                                            {"cache_", ir::Disposition::Private, ""},
                                        });
    // `widget.obs` must exist for the one Routed entry above to resolve.
    const ir::Node document_root = ir::Section{"widget.obs", "Observers", {node}};
    CHECK(validate(document_root).empty());
}

TEST_CASE("validate - direction 1: an undocumented declaration is an error naming its class") {
    const ir::Node node = synopsis_with("widget", {{"resize", ir::Disposition::Undocumented, ""}});

    const Diagnostics diags = validate(node);
    REQUIRE(diags.size() == 1);
    CHECK(diags.front().severity == Severity::Error);
    CHECK(diags.front().context == "widget/synopsis");
    CHECK(diags.front().message.find("`resize` is declared in the synopsis") != std::string::npos);
}

TEST_CASE("validate - direction 2: a description routed to a section that does not exist is an error") {
    const ir::Node node = synopsis_with("widget", {{"reserve", ir::Disposition::Routed, "widget.nowhere"}});

    const Diagnostics diags = validate(node);
    REQUIRE(diags.size() == 1);
    CHECK(diags.front().severity == Severity::Error);
    CHECK(diags.front().message.find("[widget.nowhere]") != std::string::npos);
}

TEST_CASE("validate - direction 2: a description routed to nothing at all is an error") {
    // An in-class member under no `\ref` group and carrying no `\at`. Its
    // section is empty, which is why it cannot be spelled Described: an
    // out-of-line definition's section is empty too, and that one resolves.
    const ir::Node node = synopsis_with("widget", {{"shrink", ir::Disposition::Routed, ""}});

    const Diagnostics diags = validate(node);
    REQUIRE(diags.size() == 1);
    CHECK(diags.front().severity == Severity::Error);
    CHECK(diags.front().message.find("routed to no section") != std::string::npos);
}

TEST_CASE("validate - direction 2 resolves against a section anywhere in the document, not just an ancestor") {
    // The shape the front end actually emits: the synopsis sits at the top
    // level, *before* the `\rSec` that opens the section its members' wording
    // was routed to (design §3.2). A per-root name set would call this
    // dangling.
    ir::Document doc;
    doc.nodes.push_back(synopsis_with("widget", {{"empty", ir::Disposition::Routed, "widget.obs"}}));
    doc.nodes.push_back(ir::Section{"widget.obs", "Observers", {}});
    CHECK(validate(doc).empty());
}

TEST_CASE("validate - a synopsis with no class name falls back to the bare context") {
    const ir::Node node = synopsis_with("", {{"resize", ir::Disposition::Undocumented, ""}});

    const Diagnostics diags = validate(node);
    REQUIRE(diags.size() == 1);
    CHECK(diags.front().context == "synopsis");
}

TEST_CASE("validate - coverage and span findings compose, under one nested context") {
    ir::Synopsis synopsis;
    synopsis.name   = "widget";
    synopsis.code   = {"abc", {{0, 9, ir::SpanKind::ExposId, "y"}}}; // out of range
    synopsis.roster = {{"resize", ir::Disposition::Undocumented, ""}};
    ir::Node inner  = ir::Section{"inner", "Inner", {ir::Node{synopsis}}};
    ir::Node outer  = ir::Section{"outer", "Outer", {inner}};

    const Diagnostics diags = validate(outer);
    REQUIRE(diags.size() == 2);
    // Span findings first: the Synopsis case combines them in that order, and
    // the monoid preserves it.
    CHECK(diags.at(0).message.find("exceeds") != std::string::npos);
    CHECK(diags.at(1).message.find("is declared in the synopsis") != std::string::npos);
    for (const Diagnostic& finding : diags)
        CHECK(finding.context == "outer/inner/widget/synopsis");
}

// --- a Ref span pointing at no section (design §7) -------------------------

TEST_CASE("validate - a Ref span naming a section that exists is silent") {
    ir::Document doc;
    doc.nodes.push_back(ir::Node{
        ir::Synopsis{.name = "gadget", .code = {"x", {{0, 1, ir::SpanKind::Ref, "gadget.a"}}}, .roster = {}}});
    doc.nodes.push_back(ir::Node{ir::Section{"gadget.a", "Alpha", {}}});
    CHECK(validate(doc).empty());
}

TEST_CASE("validate - a Ref span naming no section in the document is an error") {
    const ir::Node node =
        ir::Synopsis{.name = "gadget", .code = {"x", {{0, 1, ir::SpanKind::Ref, "gadget.b"}}}, .roster = {}};

    const Diagnostics diags = validate(node);
    REQUIRE(diags.size() == 1);
    CHECK(diags.front().severity == Severity::Error);
    CHECK(diags.front().context == "gadget/synopsis");
    CHECK(diags.front().message.find("[gadget.b]") != std::string::npos);
}

TEST_CASE("validate - a RefInline naming no section in the document draws no finding") {
    // The design decision this rule turns on: ir::RefInline is the *prose*
    // cross-reference (`\iref`), and prose legitimately cites subclauses
    // outside the generated document ("...is active\iref{class.union.general}"
    // in [optional]'s own wording). Widening the Ref-span rule to cover
    // RefInline too would report that citation as an error.
    const ir::Node node = ir::FreeParagraph{{ir::RefInline{"class.union.general"}}};
    CHECK(validate(node).empty());
}

// --- the leakage checker (design §9) ----------------------------------------

namespace {

// A document in the shape the front end emits: a synopsis carrying `roster`
// at the top level, and one described item beside it whose Effects is an
// extracted "Equivalent to:" body -- §9's headline leakage site.
ir::Document document_with_equiv(std::vector<ir::SynopsisEntry> roster, std::string body) {
    ir::DescriptionElement effects;
    effects.kind       = ir::ElementKind::Effects;
    effects.equivalent = ir::EquivalentTo{{std::move(body), {}}};

    ir::SpecItem item;
    item.decl.signatures.push_back({"void reset();", {}});
    item.descr.elements.push_back(std::move(effects));

    ir::Document doc;
    doc.nodes.push_back(synopsis_with("widget", std::move(roster)));
    doc.nodes.push_back(ir::Section{"widget.mod", "Modifiers", {ir::Node{std::move(item)}}});
    return doc;
}

} // namespace

TEST_CASE("validate - a leaked private member in an extracted body is an error naming the fixits") {
    const Diagnostics diags =
        validate(document_with_equiv({{"cache_", ir::Disposition::Private, ""}}, "cache_ = {};"));

    REQUIRE(diags.size() == 1);
    CHECK(diags.front().severity == Severity::Error);
    CHECK(diags.front().context == "widget.mod/effects-equiv");
    CHECK(diags.front().message.find("`cache_` is used in wording") != std::string::npos);
    CHECK(diags.front().message.find("an unmarked private member") != std::string::npos);
    // The §9 fixit trichotomy, in the message rather than in a separate note.
    CHECK(diags.front().message.find("mark it `\\expos`") != std::string::npos);
    CHECK(diags.front().message.find("rewrite the wording in documented terms") != std::string::npos);
    CHECK(diags.front().message.find("demote it to authored prose") != std::string::npos);
}

TEST_CASE("validate - every visible disposition is silent where the wording names it") {
    // The mirror of the coverage suite's "accounted for" case, and it
    // partitions the *same* dispositions differently: `\omit`ted, `\merge`d
    // and unmarked-private are accounted for there and invisible here, while
    // `\expos` -- which coverage never reports -- is the escape hatch §9's
    // fixits offer, so it must be silent.
    const Diagnostics diags = validate(document_with_equiv(
        {
            {"size", ir::Disposition::Described, ""},
            {"reset", ir::Disposition::Routed, "widget.mod"},
            {"operator=", ir::Disposition::Defaulted, ""},
            {"engaged", ir::Disposition::Expos, ""},
        },
        "engaged = false;\nreset();\nsize();\noperator=;"));
    CHECK(diags.empty());
}

TEST_CASE("validate - each invisible disposition reports its own reason") {
    const auto reason_for = [](ir::Disposition disposition, std::string name) {
        const Diagnostics diags = validate(document_with_equiv({{name, disposition, ""}}, name + "();"));
        // An Undocumented entry also draws a coverage finding; the leakage
        // one is the last, since the synopsis root folds before the section.
        REQUIRE_FALSE(diags.empty());
        return diags.back().message;
    };
    CHECK(reason_for(ir::Disposition::Private, "cache_").find("an unmarked private member") != std::string::npos);
    CHECK(reason_for(ir::Disposition::Omitted, "scratch").find("(`\\omit`ted)") != std::string::npos);
    CHECK(reason_for(ir::Disposition::Merged, "clone").find("a `\\merge`d twin") != std::string::npos);
    CHECK(reason_for(ir::Disposition::Undocumented, "hard_reset").find("declared but never described") !=
          std::string::npos);
}

TEST_CASE("validate - a name matching no roster entry is left alone") {
    // Everything the roster cannot speak for: a std entity, a template
    // parameter, a local of the extracted body, a keyword. Telling these
    // apart from a leak needs the front end's reference resolution, so the
    // conservative answer -- no finding -- is the specified one.
    const Diagnostics diags = validate(document_with_equiv({{"cache_", ir::Disposition::Private, ""}},
                                                           "T tmp = move(other);\nreturn addressof(tmp);"));
    CHECK(diags.empty());
}

TEST_CASE("validate - a documented declaration of a name outweighs an invisible one") {
    // The `nullopt_t` shape: a documented class whose constructor is
    // `\omit`ted, so a roster entry shares the class's own name. Naming the
    // *type* in wording is not a leak, and this is the case that made the
    // check a set difference rather than a lookup.
    ir::Document doc = document_with_equiv({{"nullopt_t", ir::Disposition::Omitted, ""}}, "nullopt_t x;");
    doc.nodes.push_back(ir::Synopsis{.name = "nullopt_t", .code = {"struct nullopt_t {};", {}}, .roster = {}});
    CHECK(validate(doc).empty());

    // ... and with no such class, the same wording does leak -- so the case
    // above is the class carrying it, not the name being special.
    CHECK(validate(document_with_equiv({{"nullopt_t", ir::Disposition::Omitted, ""}}, "nullopt_t x;")).size() == 1);
}

TEST_CASE("validate - one finding per leaked name per fragment, at its first appearance") {
    const Diagnostics diags = validate(
        document_with_equiv({{"cache_", ir::Disposition::Private, ""}, {"scratch", ir::Disposition::Omitted, ""}},
                            "cache_ = {};\nscratch();\ncache_.clear();"));

    REQUIRE(diags.size() == 2);
    CHECK(diags.at(0).message.find("`cache_`") != std::string::npos);
    CHECK(diags.at(1).message.find("`scratch`") != std::string::npos);
}

TEST_CASE("validate - a leak is matched whole-identifier, not as a substring") {
    // `value_` must not fire on `evaluate_` or on `x.value_ptr` -- the same
    // whole-identifier rule the front end's expos-use rewriting follows
    // (design §3.5).
    const Diagnostics diags =
        validate(document_with_equiv({{"value_", ir::Disposition::Private, ""}}, "return evaluate_(value_ptr);"));
    CHECK(diags.empty());
}

TEST_CASE("validate - leaks are reported in backticked prose and in itemized conditions") {
    ir::DescriptionElement ensures;
    ensures.kind = ir::ElementKind::Ensures;
    ensures.paragraphs.push_back({ir::CodeInline{{"scratch", {}}}, ir::TextInline{" is released"}});

    ir::DescriptionElement remarks;
    remarks.kind    = ir::ElementKind::Remarks;
    remarks.itemize = ir::Itemize{{{ir::CodeInline{{"clone", {}}}, ir::TextInline{" is not called"}}}};

    ir::SpecItem item;
    item.decl.signatures.push_back({"void reset();", {}});
    item.descr.elements = {std::move(ensures), std::move(remarks)};

    ir::Document doc;
    doc.nodes.push_back(
        synopsis_with("widget", {{"scratch", ir::Disposition::Omitted, ""}, {"clone", ir::Disposition::Merged, ""}}));
    doc.nodes.push_back(ir::Section{"widget.mod", "Modifiers", {ir::Node{std::move(item)}}});

    const Diagnostics diags = validate(doc);
    REQUIRE(diags.size() == 2);
    CHECK(diags.at(0).context == "widget.mod/ensures");
    CHECK(diags.at(1).context == "widget.mod/remarks");
}

TEST_CASE("validate - every two-dimensional table field is a leakage site") {
    ir::DescriptionElement effects;
    effects.kind  = ir::ElementKind::Effects;
    effects.table = ir::Table2D{
        .stable_name = "widget.effects",
        .caption     = {ir::CodeInline{{"caption_hidden", {}}}},
        .column1     = {ir::CodeInline{{"column_hidden", {}}}},
        .column2     = {ir::TextInline{"visible"}},
        .rows        = {{.header = {ir::CodeInline{{"row_hidden", {}}}},
                         .cell1  = {ir::CodeInline{{"cell1_hidden", {}}}},
                         .cell2  = {ir::CodeInline{{"cell2_hidden", {}}}}}},
    };

    ir::SpecItem item;
    item.decl.signatures.push_back({"void f();", {}});
    item.descr.elements.push_back(std::move(effects));

    ir::Document doc;
    doc.nodes.push_back(synopsis_with("widget",
                                      {{"caption_hidden", ir::Disposition::Private, ""},
                                       {"column_hidden", ir::Disposition::Private, ""},
                                       {"row_hidden", ir::Disposition::Private, ""},
                                       {"cell1_hidden", ir::Disposition::Private, ""},
                                       {"cell2_hidden", ir::Disposition::Private, ""}}));
    doc.nodes.push_back(ir::Section{"widget.effects", "Effects", {ir::Node{std::move(item)}}});

    const Diagnostics diags = validate(doc);
    REQUIRE(diags.size() == 5);
    CHECK(std::ranges::all_of(diags, [](const Diagnostic& diag) { return diag.context == "widget.effects/effects"; }));
}

TEST_CASE("validate - spans are checked in every description CodeText") {
    const auto malformed_inline = [] {
        return ir::CodeInline{ir::CodeText{"x", {{0, 2, ir::SpanKind::ExposId, "x"}}}};
    };

    ir::DescriptionElement effects;
    effects.kind       = ir::ElementKind::Effects;
    effects.paragraphs = {{malformed_inline()}};
    effects.itemize    = ir::Itemize{{{malformed_inline()}}};
    effects.table      = ir::Table2D{
        .stable_name = "widget.effects",
        .caption     = {malformed_inline()},
        .column1     = {malformed_inline()},
        .column2     = {malformed_inline()},
        .rows = {{.header = {malformed_inline()}, .cell1 = {malformed_inline()}, .cell2 = {malformed_inline()}}},
    };
    effects.equivalent = ir::EquivalentTo{{"x", {{0, 2, ir::SpanKind::ExposId, "x"}}}};

    ir::SpecItem item;
    item.decl.signatures.push_back({"void f();", {}});
    item.descr.elements.push_back(std::move(effects));

    const Diagnostics diags = validate(ir::Node{item});
    REQUIRE(diags.size() == 9);
    CHECK(std::ranges::count_if(diags, [](const Diagnostic& diag) { return diag.context == "effects"; }) == 8);
    CHECK(std::ranges::count_if(diags, [](const Diagnostic& diag) { return diag.context == "effects-equiv"; }) == 1);
    CHECK(std::ranges::all_of(
        diags, [](const Diagnostic& diag) { return diag.message == "span 0 end 2 exceeds text length 1"; }));
}

TEST_CASE("validate - malformed tables remain invalid after a JSON round trip") {
    ir::SpecItem item;
    item.decl.signatures.push_back({"void f();", {}});
    ir::DescriptionElement effects;
    effects.kind  = ir::ElementKind::Effects;
    effects.table = ir::Table2D{.rows = {ir::Table2DRow{}}};
    item.descr.elements.push_back(std::move(effects));

    const auto parsed = ir::parse_item(ir::emit_json(item));
    REQUIRE(parsed.has_value());
    const Diagnostics empty_fields = validate(ir::Node{*parsed});
    REQUIRE(empty_fields.size() == 7);
    CHECK(std::ranges::all_of(empty_fields, [](const Diagnostic& diag) {
        return diag.context == "effects/table" && diag.severity == Severity::Error;
    }));
    CHECK(empty_fields.at(4).message == "table row 1 header is empty");
    CHECK(empty_fields.at(5).message == "table row 1 first cell is empty");
    CHECK(empty_fields.at(6).message == "table row 1 second cell is empty");

    ir::SpecItem no_rows = *parsed;
    no_rows.descr.elements.front().table->rows.clear();
    const auto reparsed = ir::parse_item(ir::emit_json(no_rows));
    REQUIRE(reparsed.has_value());
    const Diagnostics missing_row = validate(ir::Node{*reparsed});
    REQUIRE(missing_row.size() == 5);
    CHECK(missing_row.back().message == "table requires at least one row");
}

TEST_CASE("validate - an itemdecl and a free paragraph are leakage sites too") {
    ir::SpecItem item;
    item.decl.signatures.push_back({"void reset(cache_& c);", {}});

    ir::Document doc;
    doc.nodes.push_back(synopsis_with("widget", {{"cache_", ir::Disposition::Private, ""}}));
    doc.nodes.push_back(ir::Section{"widget.mod", "Modifiers", {ir::Node{std::move(item)}}});
    doc.nodes.push_back(ir::FreeParagraph{{ir::TextInline{"Leaves "}, ir::CodeInline{{"cache_", {}}}}});

    const Diagnostics diags = validate(doc);
    REQUIRE(diags.size() == 2);
    CHECK(diags.at(0).context == "widget.mod/itemdecl[0]");
    CHECK(diags.at(1).context == "paragraph");
}

TEST_CASE("validate - visibility resolves across the whole forest, not per root") {
    // The synopsis that makes `engaged` visible is a sibling root of the
    // section holding the wording that names it -- exactly the shape
    // `generate --emit-ir` produces.
    CHECK(validate(document_with_equiv({{"engaged", ir::Disposition::Expos, ""}}, "engaged = false;")).empty());
}

// --- the leakage checker, resolved half (design §9) -------------------------

namespace {

// The same document, plus the one thing only the front end knows: a namespace
// qualifier its reference-resolved mapping did not drop.
ir::Document document_with_foreign(std::vector<ir::SynopsisEntry>    roster,
                                   std::string                       body,
                                   std::vector<ir::ForeignNamespace> foreign) {
    ir::Document doc       = document_with_equiv(std::move(roster), std::move(body));
    doc.foreign_namespaces = std::move(foreign);
    return doc;
}

} // namespace

TEST_CASE("validate - a surviving namespace qualifier in an extracted body is an error naming what it resolved to") {
    const Diagnostics diags =
        validate(document_with_foreign({}, "return detail::make();", {{"detail", "demo::detail"}}));

    REQUIRE(diags.size() == 1);
    CHECK(diags.front().severity == Severity::Error);
    CHECK(diags.front().context == "widget.mod/effects-equiv");
    CHECK(diags.front().message.find("`detail` appears in rendered output") != std::string::npos);
    CHECK(diags.front().message.find("a namespace qualifier resolving to `demo::detail`") != std::string::npos);
    // Not the roster half's trichotomy: there is no namespace to mark
    // `\expos`, and the name is not the author's to demote.
    CHECK(diags.front().message.find("rewrite the name in documented terms") != std::string::npos);
    CHECK(diags.front().message.find("move the entity it names into the specified namespace") != std::string::npos);
    CHECK(diags.front().message.find("\\expos") == std::string::npos);
}

TEST_CASE("validate - a synopsis is checked for a surviving qualifier and not for a hidden roster name") {
    // The asymmetry that keeps the two halves apart: a synopsis is where
    // names are *declared*, so its own text naming a `\merge`d twin is not a
    // leak -- but a qualifier is wrong there for exactly the reason it is
    // wrong in a body.
    ir::Document doc;
    doc.nodes.push_back(ir::Synopsis{.name = "widget",
                                     .code = {"class widget {\n  detail::storage store_;\n  widget clone();\n};", {}},
                                     .roster = {{"clone", ir::Disposition::Merged, ""}}});
    doc.foreign_namespaces = {{"detail", "demo::detail"}};

    const Diagnostics diags = validate(doc);
    REQUIRE(diags.size() == 1);
    CHECK(diags.front().context == "widget/synopsis");
    CHECK(diags.front().message.find("`detail` appears in rendered output") != std::string::npos);
}

TEST_CASE("validate - a foreign namespace name used as a bare word is not a qualifier finding") {
    ir::Document doc;
    doc.nodes.push_back(ir::Synopsis{
        .name = "widget", .code = {"class widget {\n  // detail is exposition only\n};", {}}, .roster = {}});
    doc.foreign_namespaces = {{"detail", "demo::detail"}};

    CHECK(validate(doc).empty());
}

TEST_CASE("validate - a documented declaration outweighs a foreign namespace of the same name") {
    // The documented-wins rule, reaching the qualifier channel: a class the
    // reader can see is what the name means, whichever way the other reading
    // is hidden.
    ir::Document doc = document_with_foreign({}, "return impl::spin();", {{"impl", "demo::impl"}});
    doc.nodes.push_back(ir::Synopsis{.name = "impl", .code = {"struct impl {};", {}}, .roster = {}});

    CHECK(validate(doc).empty());
}

TEST_CASE("validate - a name in neither the roster nor the foreign list is left alone") {
    CHECK(validate(document_with_foreign({}, "return move(tmp);", {{"detail", "demo::detail"}})).empty());
}

TEST_CASE("validate - one qualifier finding per fragment, however often the name appears") {
    const Diagnostics diags =
        validate(document_with_foreign({}, "detail::reset();\ndetail::seal();", {{"detail", "demo::detail"}}));
    CHECK(diags.size() == 1);
}

TEST_CASE("validate - validate(Node) reports no qualifier findings: the channel is the document's") {
    // A lone node is its own document for section names and for rosters,
    // both of which it carries. A surviving qualifier it does not: nothing
    // in a node records that the front end resolved one.
    const ir::Document doc = document_with_foreign({}, "return detail::make();", {{"detail", "demo::detail"}});
    CHECK(validate(doc.nodes.at(1)).empty());
}

// --- a helper named only by a non-extracted body (design §9) ----------------

namespace {

// The same document one more channel wider: what the front end saw the bodies
// it never rendered reach for (ir::BodyUse). `document_with_equiv`'s own
// extracted body is what the "only" clause has to be told apart from.
ir::Document
document_with_body_uses(std::vector<ir::SynopsisEntry> roster, std::string body, std::vector<ir::BodyUse> uses) {
    ir::Document doc     = document_with_equiv(std::move(roster), std::move(body));
    doc.unextracted_uses = std::move(uses);
    return doc;
}

} // namespace

TEST_CASE("validate - a hidden helper used only by an unrendered body is a note naming that body") {
    const Diagnostics diags =
        validate(document_with_body_uses({{"hard_reset", ir::Disposition::Private, "", ir::MemberKind::Function}},
                                         "engaged_ = false;",
                                         {{"widget::widget", "hard_reset"}}));

    REQUIRE(diags.size() == 1);
    // A Note, not an Error: nothing in the rendered wording is wrong, which
    // is the whole distinction §9 draws between its two severities.
    CHECK(diags.front().severity == Severity::Note);
    CHECK(diags.front().context == "widget::widget/body");
    CHECK(diags.front().message.find("`hard_reset` is used by this body") != std::string::npos);
    CHECK(diags.front().message.find("an unmarked private member") != std::string::npos);
    CHECK(diags.front().message.find("nothing leaks yet") != std::string::npos);
    // The fixit names the marker the author would write, not the draft
    // vocabulary it produces: `\effects-equiv` is what turns this note into
    // the leakage checker's error.
    CHECK(diags.front().message.find("before giving this body an `\\effects-equiv`") != std::string::npos);
}

TEST_CASE("validate - a helper the wording already leaked draws the error only, never a second note") {
    // §9's "only": the same name in the extracted body *and* in a body that
    // is never rendered. check_leakage owns that case, at Error severity.
    const Diagnostics diags =
        validate(document_with_body_uses({{"hard_reset", ir::Disposition::Omitted, "", ir::MemberKind::Function}},
                                         "hard_reset();",
                                         {{"widget::reset", "hard_reset"}}));

    REQUIRE(diags.size() == 1);
    CHECK(diags.front().severity == Severity::Error);
    CHECK(diags.front().context == "widget.mod/effects-equiv");
}

TEST_CASE("validate - hidden private data used by an unrendered body is left to the private-data nudge") {
    // The narrowing that keeps this rule quiet: §9 says *helper*, and the
    // hidden-data case is design §6's, reported once at the declaration
    // rather than once per body that reads it. Dropping this filter reports
    // seventeen filler members across eleven corpus headers.
    CHECK(validate(document_with_body_uses({{"cache_", ir::Disposition::Private, "", ir::MemberKind::Data}},
                                           "engaged_ = false;",
                                           {{"widget::reset", "cache_"}}))
              .empty());
}

TEST_CASE("validate - a visible member, and a name in no roster, are both silent in a body") {
    CHECK(validate(document_with_body_uses({{"size", ir::Disposition::Described, "", ir::MemberKind::Function}},
                                           "engaged_ = false;",
                                           {{"widget::reset", "size"}, {"widget::reset", "tmp"}}))
              .empty());
}

TEST_CASE("validate - one helper reached by two bodies is reported once per body") {
    // The finding is about the body, not the declaration: a reader fixing it
    // goes to a body. (The per-declaration reading is the private-data nudge,
    // a different rule with a different trigger.)
    const Diagnostics diags =
        validate(document_with_body_uses({{"clone", ir::Disposition::Merged, "", ir::MemberKind::Function}},
                                         "engaged_ = false;",
                                         {{"widget::reset", "clone"}, {"widget::swap", "clone"}}));

    REQUIRE(diags.size() == 2);
    CHECK(diags.at(0).context == "widget::reset/body");
    CHECK(diags.at(1).context == "widget::swap/body");
    CHECK(diags.at(0).message.find("a `\\merge`d twin") != std::string::npos);
}

TEST_CASE("validate - validate(Node) reports no body-use findings: the channel is the document's") {
    // Same reason validate(Node) reports no qualifier: a lone node carries a
    // roster, but nothing in it records what an unrendered body reached for.
    const ir::Document doc =
        document_with_body_uses({{"hard_reset", ir::Disposition::Private, "", ir::MemberKind::Function}},
                                "engaged_ = false;",
                                {{"widget::widget", "hard_reset"}});
    CHECK(validate(doc.nodes.at(1)).empty());
}

// --- Mandates/Constraints drift (design §5.2) -------------------------------

namespace {

// A derived conjunct in the `phrase_conjunct` (frontend.cpp) shape: a bool
// trait/negation reads `` `subject` is `polarity` ``.
ir::Paragraph conjunct_paragraph(std::string subject, std::string polarity) {
    return {
        ir::CodeInline{{std::move(subject), {}}}, ir::TextInline{" is "}, ir::CodeInline{{std::move(polarity), {}}}};
}

// The same shape, as an author would write it by hand in a docblock (the
// corpus convention `spec_inclass_markers.hpp` uses): a trailing period
// after the closing backtick.
ir::Paragraph authored_paragraph(std::string subject, std::string polarity) {
    ir::Paragraph p = conjunct_paragraph(std::move(subject), std::move(polarity));
    p.push_back(ir::TextInline{"."});
    return p;
}

} // namespace

TEST_CASE("validate - an authored Mandates duplicating its derived conjunct warns") {
    ir::DescriptionElement authored;
    authored.kind      = ir::ElementKind::Mandates;
    authored.conjuncts = {conjunct_paragraph("is_copy_constructible_v<int>", "true")};
    authored.paragraphs.push_back(authored_paragraph("is_copy_constructible_v<int>", "true"));

    ir::SpecItem item;
    item.decl.signatures.push_back({"gadget(const gadget&);", {}});
    item.descr.elements = {authored};

    const Diagnostics diags = validate(ir::Node{item});
    REQUIRE(diags.size() == 1);
    CHECK(diags.front().severity == Severity::Warning);
    CHECK(diags.front().context == "mandates");
    CHECK(diags.front().message.find("duplicates") != std::string::npos);
    CHECK(diags.front().message.find("`is_copy_constructible_v<int>`") != std::string::npos);
}

TEST_CASE("validate - an authored Mandates contradicting its derived conjunct warns, naming both readings") {
    ir::DescriptionElement authored;
    authored.kind      = ir::ElementKind::Mandates;
    authored.conjuncts = {conjunct_paragraph("is_copy_constructible_v<int>", "true")};
    authored.paragraphs.push_back(authored_paragraph("is_copy_constructible_v<int>", "false"));

    ir::SpecItem item;
    item.decl.signatures.push_back({"gadget(const gadget&);", {}});
    item.descr.elements = {authored};

    const Diagnostics diags = validate(ir::Node{item});
    REQUIRE(diags.size() == 1);
    CHECK(diags.front().message.find("contradicts") != std::string::npos);
    CHECK(diags.front().message.find("derived `true`") != std::string::npos);
    CHECK(diags.front().message.find("authored `false`") != std::string::npos);
}

TEST_CASE("validate - an authored Constraints element compares the same way") {
    ir::DescriptionElement authored;
    authored.kind      = ir::ElementKind::Constraints;
    authored.conjuncts = {conjunct_paragraph("copyable<T>", "true")};
    authored.paragraphs.push_back(authored_paragraph("copyable<T>", "true"));

    ir::SpecItem item;
    item.decl.signatures.push_back({"template<class T> void f(T);", {}});
    item.descr.elements = {authored};

    const Diagnostics diags = validate(ir::Node{item});
    REQUIRE(diags.size() == 1);
    CHECK(diags.front().context == "constraints");
    CHECK(diags.front().message.find("duplicates") != std::string::npos);
}

TEST_CASE("validate - drift comparison reads authored table cells") {
    ir::DescriptionElement authored;
    authored.kind      = ir::ElementKind::Constraints;
    authored.conjuncts = {conjunct_paragraph("copyable<T>", "true")};
    authored.table     = ir::Table2D{
        .stable_name = "widget.constraints",
        .caption     = {ir::TextInline{"Conditions"}},
        .column1     = {ir::TextInline{"yes"}},
        .column2     = {ir::TextInline{"no"}},
        .rows        = {{.header = {ir::TextInline{"T"}},
                         .cell1  = authored_paragraph("copyable<T>", "true"),
                         .cell2  = {ir::TextInline{"otherwise"}}}},
    };

    ir::SpecItem item;
    item.decl.signatures.push_back({"template<class T> void f(T);", {}});
    item.descr.elements = {authored};

    const Diagnostics diags = validate(ir::Node{item});
    REQUIRE(diags.size() == 1);
    CHECK(diags.front().message.find("duplicates") != std::string::npos);
}

TEST_CASE("validate - authored prose not mentioning the derived subject is silent") {
    ir::DescriptionElement authored;
    authored.kind      = ir::ElementKind::Mandates;
    authored.conjuncts = {conjunct_paragraph("is_copy_constructible_v<int>", "true")};
    authored.paragraphs.push_back({ir::TextInline{"Some unrelated authored condition."}});

    ir::SpecItem item;
    item.decl.signatures.push_back({"gadget(const gadget&);", {}});
    item.descr.elements = {authored};

    CHECK(validate(ir::Node{item}).empty());
}

TEST_CASE("validate - a derived element with no authored twin is silent") {
    ir::DescriptionElement derived;
    derived.kind      = ir::ElementKind::Mandates;
    derived.derived   = true;
    derived.conjuncts = {conjunct_paragraph("is_copy_constructible_v<int>", "true")};

    ir::SpecItem item;
    item.decl.signatures.push_back({"gadget(const gadget&);", {}});
    item.descr.elements = {derived};

    CHECK(validate(ir::Node{item}).empty());
}

TEST_CASE("validate - a derived conjunct with no readable polarity (a concept-id) is not compared") {
    // phrase_conjunct's concept-id shape has no trailing true/false literal,
    // so read_conjunct reads it Unknown -- the drift check must not guess.
    ir::DescriptionElement authored;
    authored.kind      = ir::ElementKind::Constraints;
    authored.conjuncts = {{ir::CodeInline{{"U", {}}}, ir::TextInline{" models "}, ir::ConceptRef{"copyable"}}};
    authored.paragraphs.push_back(authored_paragraph("U", "true"));

    ir::SpecItem item;
    item.decl.signatures.push_back({"template<class U> void f(U);", {}});
    item.descr.elements = {authored};

    CHECK(validate(ir::Node{item}).empty());
}

// --- the unmarked-private-data nudge (design §6) ----------------------------

namespace {

ir::SynopsisEntry roster_entry(std::string name, ir::Disposition disposition, ir::MemberKind kind) {
    return ir::SynopsisEntry{.name = std::move(name), .disposition = disposition, .section = {}, .kind = kind};
}

} // namespace

TEST_CASE("validate - unmarked private data in a class that exposes other state is a note") {
    ir::Node node = synopsis_with("optional",
                                  {roster_entry("value_", ir::Disposition::Expos, ir::MemberKind::Data),
                                   roster_entry("engaged_", ir::Disposition::Private, ir::MemberKind::Data)});

    const Diagnostics diags = validate(node);
    REQUIRE(diags.size() == 1);
    CHECK(diags.front().severity == Severity::Note);
    CHECK(diags.front().context == "optional/synopsis");
    CHECK(diags.front().message.find("`engaged_`") != std::string::npos);
    CHECK(diags.front().message.find("\\expos") != std::string::npos);
}

TEST_CASE("validate - a class that marks nothing expos is silent, whatever private data it has") {
    // The narrowing that keeps this rule off ten of the fifteen corpus
    // headers: private data with no `\expos` anywhere in the class is filler,
    // not state the wording leans on. Deleting the `exposes_state` guard in
    // check_private_data fails here.
    ir::Node node = synopsis_with("widget",
                                  {roster_entry("data_", ir::Disposition::Private, ir::MemberKind::Data),
                                   roster_entry("other_", ir::Disposition::Private, ir::MemberKind::Data)});
    CHECK(validate(node).empty());
}

TEST_CASE("validate - an unmarked private function is silent even in a class that exposes state") {
    // design §6: "Private fn, unmarked | omitted, silent". Only data is
    // nudged, which is what the roster's `kind` field exists to distinguish.
    ir::Node node = synopsis_with("optional",
                                  {roster_entry("value_", ir::Disposition::Expos, ir::MemberKind::Data),
                                   roster_entry("hard_reset", ir::Disposition::Private, ir::MemberKind::Function)});
    CHECK(validate(node).empty());
}

TEST_CASE("validate - an alias is not private data") {
    ir::Node node = synopsis_with("optional",
                                  {roster_entry("value_", ir::Disposition::Expos, ir::MemberKind::Data),
                                   roster_entry("iterator", ir::Disposition::Private, ir::MemberKind::Alias)});
    CHECK(validate(node).empty());
}

TEST_CASE("validate - private data that is nonetheless described is not nudged") {
    ir::Node node = synopsis_with("optional",
                                  {roster_entry("value_", ir::Disposition::Expos, ir::MemberKind::Data),
                                   roster_entry("size_", ir::Disposition::Described, ir::MemberKind::Data)});
    CHECK(validate(node).empty());
}

TEST_CASE("validate - a class whose data is all marked expos is silent") {
    ir::Node node = synopsis_with("handle", {roster_entry("fd_", ir::Disposition::Expos, ir::MemberKind::Data)});
    CHECK(validate(node).empty());
}

TEST_CASE("validate - an expos private function is enough to arm the nudge") {
    // The guard asks whether the author is using the exposition-only
    // mechanism at all, not whether they used it on *data* -- a class that
    // marks a helper `\expos` and leaves its state unmarked is the same
    // inconsistency.
    ir::Node node = synopsis_with("optional",
                                  {roster_entry("check", ir::Disposition::Expos, ir::MemberKind::Function),
                                   roster_entry("engaged_", ir::Disposition::Private, ir::MemberKind::Data)});
    REQUIRE(validate(node).size() == 1);
}

TEST_CASE("validate - the nudge is a Note, so --validate still renders") {
    ir::Node node = synopsis_with("optional",
                                  {roster_entry("value_", ir::Disposition::Expos, ir::MemberKind::Data),
                                   roster_entry("engaged_", ir::Disposition::Private, ir::MemberKind::Data)});
    CHECK_FALSE(has_errors(validate(node)));
}

TEST_CASE("validate - a roster entry defaults to Function, so a golden omitting `kind` reports nothing") {
    ir::Node node = synopsis_with("optional",
                                  {ir::SynopsisEntry{.name = "value_", .disposition = ir::Disposition::Expos},
                                   ir::SynopsisEntry{.name = "engaged_", .disposition = ir::Disposition::Private}});
    CHECK(validate(node).empty());
}

// --- noexcept <-> *Throws:* (design §5.4) -----------------------------------

namespace {

// An item with @p signatures and one authored *Throws:* reading @p claim.
// The check is entirely about the relationship between those two, so
// everything else stays default.
ir::SpecItem throwing_item(std::vector<std::string> signatures, std::string claim) {
    ir::SpecItem item;
    item.decl.signatures = signatures |
                           std::views::transform([](const std::string& s) { return ir::CodeText{s, {}}; }) |
                           std::ranges::to<std::vector>();
    ir::DescriptionElement throws;
    throws.kind = ir::ElementKind::Throws;
    throws.paragraphs.push_back({ir::TextInline{std::move(claim)}});
    item.descr.elements = {throws};
    return item;
}

} // namespace

TEST_CASE("validate - a noexcept signature whose authored Throws says it throws warns, naming the signature") {
    const Diagnostics diags =
        validate(ir::Node{throwing_item({"void f() noexcept;"}, "std::bad_alloc if allocation fails.")});
    REQUIRE(diags.size() == 1);
    CHECK(diags.front().severity == Severity::Warning);
    CHECK(diags.front().context == "throws");
    CHECK(diags.front().message.find("`void f() noexcept;`") != std::string::npos);
    CHECK(diags.front().message.find("take `noexcept` off the declaration") != std::string::npos);
}

TEST_CASE("validate - neither noexcept finding is an Error, so --validate still renders") {
    // Not a restatement of the severity checks above: `has_errors` is what
    // tools/specgen/main.cpp branches on to skip rendering entirely, so this
    // is the assertion that a wrong *Throws:* paragraph does not cost the
    // whole document. Promoting either finding to Error fails here.
    CHECK_FALSE(has_errors(validate(ir::Node{throwing_item({"void f() noexcept;"}, "std::bad_alloc.")})));
    CHECK_FALSE(has_errors(validate(ir::Node{throwing_item({"void f() noexcept;"}, "Nothing.")})));
}

TEST_CASE("validate - a noexcept signature whose authored Throws reads Nothing is the redundancy finding") {
    const Diagnostics diags = validate(ir::Node{throwing_item({"void f() noexcept;"}, "Nothing.")});
    REQUIRE(diags.size() == 1);
    CHECK(diags.front().severity == Severity::Warning);
    CHECK(diags.front().message.find("adds nothing") != std::string::npos);
}

TEST_CASE("validate - Nothing is recognized regardless of case, surrounding space, and trailing periods") {
    for (const std::string claim : {"Nothing.", "nothing", "  NOTHING. ", "Nothing.."}) {
        const Diagnostics diags = validate(ir::Node{throwing_item({"void f() noexcept;"}, claim)});
        REQUIRE(diags.size() == 1);
        CHECK(diags.front().severity == Severity::Warning);
    }
}

TEST_CASE("validate - a Throws element saying more than Nothing is a contradiction even if it mentions nothing") {
    const Diagnostics diags =
        validate(ir::Node{throwing_item({"void f() noexcept;"}, "Nothing unless T's destructor throws.")});
    REQUIRE(diags.size() == 1);
    CHECK(diags.front().message.find("says it throws") != std::string::npos);
}

TEST_CASE("validate - a signature with no exception specification is silent") {
    CHECK(validate(ir::Node{throwing_item({"void f();"}, "std::bad_alloc if allocation fails.")}).empty());
}

TEST_CASE("validate - Throws Nothing on a signature that is not noexcept is silent (the Lakos Rule)") {
    // There is no reverse direction, and this is why: a narrow-contract
    // function is deliberately not `noexcept` even when its implementation
    // visibly never throws, so that checking the precondition and throwing
    // stays in bounds. Nudging this toward `noexcept` would be wrong, not
    // merely noisy.
    CHECK(validate(ir::Node{throwing_item({"const T& front() const;"}, "Nothing.")}).empty());
}

TEST_CASE("validate - noexcept(true) reads as unconditional and noexcept(false) as none") {
    CHECK(validate(ir::Node{throwing_item({"void f() noexcept(true);"}, "std::bad_alloc.")}).size() == 1);
    CHECK(validate(ir::Node{throwing_item({"void f() noexcept(false);"}, "std::bad_alloc.")}).empty());
}

TEST_CASE("validate - a conditional noexcept is not comparable, so it is silent") {
    CHECK(validate(ir::Node{throwing_item({"void swap(widget&) noexcept(is_nothrow_swappable_v<T>);"},
                                          "Any exception thrown by swapping.")})
              .empty());
}

TEST_CASE("validate - a noexcept nested inside a conditional's own argument does not read as the outer one") {
    // `noexcept(noexcept(g()))` is conditional; the inner token sits at
    // bracket depth 1 and must not be picked up as a bare specifier.
    CHECK(validate(ir::Node{throwing_item({"void f() noexcept(noexcept(g()));"}, "Whatever `g` throws.")}).empty());
}

TEST_CASE("validate - a noexcept on a function-pointer parameter is not the function's own") {
    CHECK(validate(ir::Node{throwing_item({"void probe(void (*p)() noexcept);"}, "Any exception thrown by `p`.")})
              .empty());
}

TEST_CASE("validate - a grouped overload set is judged only when every signature is noexcept") {
    CHECK(validate(ir::Node{throwing_item({"const T& at(size_t) const;", "const T& at(size_t, nothrow_t) noexcept;"},
                                          "out_of_range if out of range.")})
              .empty());
    CHECK(
        validate(ir::Node{throwing_item({"void f() noexcept;", "void f(int) noexcept;"}, "std::bad_alloc.")}).size() ==
        1);
}

TEST_CASE("validate - an item with no signatures at all is silent") {
    ir::SpecItem item = throwing_item({}, "std::bad_alloc.");
    CHECK(validate(ir::Node{item}).empty());
}

TEST_CASE("validate - a noexcept item carrying no Throws element is silent") {
    ir::SpecItem item;
    item.decl.signatures.push_back({"void f() noexcept;", {}});
    ir::DescriptionElement effects;
    effects.kind = ir::ElementKind::Effects;
    effects.paragraphs.push_back({ir::TextInline{"Does nothing."}});
    item.descr.elements = {effects};
    CHECK(validate(ir::Node{item}).empty());
}

TEST_CASE("validate - an empty Throws element has nothing to contradict") {
    ir::SpecItem item;
    item.decl.signatures.push_back({"void f() noexcept;", {}});
    ir::DescriptionElement throws;
    throws.kind         = ir::ElementKind::Throws;
    item.descr.elements = {throws};
    CHECK(validate(ir::Node{item}).empty());
}

TEST_CASE("validate - a Throws element carrying itemized conditions says more than Nothing") {
    ir::SpecItem item;
    item.decl.signatures.push_back({"void f() noexcept;", {}});
    ir::DescriptionElement throws;
    throws.kind = ir::ElementKind::Throws;
    throws.paragraphs.push_back({ir::TextInline{"Nothing."}});
    throws.itemize      = ir::Itemize{{{ir::TextInline{"unless the allocator throws."}}}};
    item.descr.elements = {throws};

    const Diagnostics diags = validate(ir::Node{item});
    REQUIRE(diags.size() == 1);
    CHECK(diags.front().message.find("says it throws") != std::string::npos);
}

// --- has_errors --------------------------------------------------------------

TEST_CASE("validate - has_errors is false for a warning/note-only set") {
    const Diagnostics diags{
        {Severity::Warning, "ctx", "msg"},
        {Severity::Note, "ctx", "msg"},
    };
    CHECK_FALSE(has_errors(diags));
}

TEST_CASE("validate - has_errors is true once an Error finding is present") {
    const Diagnostics diags{
        {Severity::Warning, "ctx", "msg"},
        {Severity::Error, "ctx", "msg"},
    };
    CHECK(has_errors(diags));
}

TEST_CASE("validate - has_errors is false for an empty set") { CHECK_FALSE(has_errors(Diagnostics{})); }

// --- format_diagnostic --------------------------------------------------------

TEST_CASE("validate - format_diagnostic renders <context>: <severity>: <message>") {
    const Diagnostic error{Severity::Error, "synopsis", "span 0 is inverted: begin 5 > end 2"};
    CHECK(format_diagnostic(error) == "synopsis: error: span 0 is inverted: begin 5 > end 2");

    const Diagnostic warning{Severity::Warning, "outer/inner", "some finding"};
    CHECK(format_diagnostic(warning) == "outer/inner: warning: some finding");

    const Diagnostic note{Severity::Note, "ctx", "some note"};
    CHECK(format_diagnostic(note) == "ctx: note: some note");
}
