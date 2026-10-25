// tests/beman/specgen/ir_json.test.cpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/ir_json.hpp>
#include <beman/specgen/ir_json.hpp> // Re-inclusion verification

#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace beman::specgen::ir;

TEST_CASE("ir_json - JSON emission of a value_or-shaped item") {
    // value_or-shaped item: one signature with an exposid span, Mandates prose,
    // Effects: Equivalent to.
    SpecItem item;
    CodeText sig;
    sig.text = "template<class U = remove_cv_t<T>> constexpr remove_cv_t<T> "
               "value_or(U&& u) const &;";
    item.decl.signatures.push_back(sig);
    item.decl.index.push_back({IndexKind::Member, "value_or", "optional"});

    DescriptionElement mand;
    mand.kind = ElementKind::Mandates;
    Paragraph p;
    p.push_back(CodeInline{{"is_copy_constructible_v<T>", {}}});
    p.push_back(TextInline{" is "});
    p.push_back(CodeInline{{"true", {}}});
    p.push_back(TextInline{"."});
    mand.paragraphs.push_back(std::move(p));
    item.descr.elements.push_back(std::move(mand));

    DescriptionElement eff;
    eff.kind = ElementKind::Effects;
    CodeText body;
    body.text = "return has_value() ? **this : static_cast<T>(std::forward<U>(u));";
    // pretend has_value were exposition-only, to exercise span emission
    body.spans.push_back({7, 16, SpanKind::ExposId, "has-value"});
    eff.equivalent = EquivalentTo{std::move(body)};
    item.descr.elements.push_back(std::move(eff));

    const std::string json = emit_json(item);

    CHECK(json.find("\"type\":\"item\"") != std::string::npos);
    CHECK(json.find("\"kind\":\"mandates\"") != std::string::npos);
    CHECK(json.find("\"equivalent\"") != std::string::npos);
    CHECK(json.find("\"exposid\"") != std::string::npos);
    CHECK(json.find("\"payload\":\"has-value\"") != std::string::npos);
    CHECK(json.find("\"member\"") != std::string::npos);
    CHECK(json.find("value_or") != std::string::npos);
}

TEST_CASE("ir_json - two-dimensional tables round-trip through JSON") {
    SpecItem item;
    item.decl.signatures.push_back({"optional& operator=(const optional& rhs);", {}});

    DescriptionElement effects;
    effects.kind  = ElementKind::Effects;
    effects.table = Table2D{
        .stable_name = "optional.assign.copy",
        .caption     = {CodeInline{{"optional::operator=(const optional&)", {}}}, TextInline{" effects"}},
        .column1     = {CodeInline{{"*this", {}}}, TextInline{" contains a value"}},
        .column2     = {CodeInline{{"*this", {}}}, TextInline{" does not contain a value"}},
        .rows        = {{.header = {CodeInline{{"rhs", {}}}, TextInline{" contains a value"}},
                         .cell1  = {TextInline{"assigns the contained value"}},
                         .cell2  = {TextInline{"direct-non-list-initializes the contained value"}}}},
    };
    item.descr.elements.push_back(std::move(effects));

    const std::string json = emit_json(item);
    CHECK(json.find("\"table\"") != std::string::npos);
    CHECK(json.find("\"stable\":\"optional.assign.copy\"") != std::string::npos);
    const auto parsed = parse_item(json);
    REQUIRE(parsed.has_value());
    const auto& table = parsed->descr.elements.front().table;
    REQUIRE(table.has_value());
    CHECK(table->stable_name == "optional.assign.copy");
    REQUIRE(table->rows.size() == 1);
    CHECK(std::get<TextInline>(table->rows.front().cell2.front()).text.starts_with("direct-non-list"));
    CHECK(emit_json(*parsed) == json);
}

TEST_CASE("ir_json - flat two-column tables round-trip through JSON") {
    SpecItem item;
    item.decl.signatures.push_back({"enum class whatwg_error { invalid_byte, truncated_sequence };", {}});

    DescriptionElement remarks;
    remarks.kind       = ElementKind::Remarks;
    remarks.flat_table = Table1D{
        .stable_name = "demo.errors.tab",
        .caption     = {CodeInline{{"whatwg_error", {}}}, TextInline{" meanings"}},
        .column1     = {TextInline{"Constant"}},
        .column2     = {TextInline{"Meaning"}},
        .rows        = {{.cell1 = {CodeInline{{"invalid_byte", {}}}},
                         .cell2 = {TextInline{"the input holds an invalid byte"}}}},
    };
    item.descr.elements.push_back(std::move(remarks));

    const std::string json = emit_json(item);
    CHECK(json.find("\"flat_table\"") != std::string::npos);
    CHECK(json.find("\"stable\":\"demo.errors.tab\"") != std::string::npos);
    const auto parsed = parse_item(json);
    REQUIRE(parsed.has_value());
    const auto& table = parsed->descr.elements.front().flat_table;
    REQUIRE(table.has_value());
    CHECK(table->stable_name == "demo.errors.tab");
    REQUIRE(table->rows.size() == 1);
    CHECK(std::get<CodeInline>(table->rows.front().cell1.front()).code.text == "invalid_byte");
    CHECK(emit_json(*parsed) == json);
}

TEST_CASE("ir_json - document structure emission") {
    Document doc;
    Section  sec;
    sec.stable_name = "optional.ctor";
    sec.title       = "Constructors";
    sec.children.push_back(Synopsis{.name = {}, .code = {"constexpr optional() noexcept;", {}}, .roster = {}});
    doc.nodes.push_back(std::move(sec));

    const std::string json = emit_json(doc);
    CHECK(json.find("\"stable\":\"optional.ctor\"") != std::string::npos);
    CHECK(json.find("\"type\":\"synopsis\"") != std::string::npos);
}

// --- deserialization -------------------------------------------------------

namespace {

std::string to_json(const Document& doc) { return emit_json(doc); }

// A failed parse rendered for Catch2's INFO. With the error carried inside
// the result (decision expected-error-taxonomy) rather than written through an
// out-param, there is no `ParseError error;` to declare ahead of the call and
// no way to read a stale one after a success.
template <class T>
std::string parse_failure(const std::expected<T, ParseError>& result) {
    return result ? std::string{"parsed"}
                  : "parse error at " + std::to_string(result.error().offset) + ": " + result.error().message;
}

// The round-trip invariant: re-emitting a parsed document reproduces the
// bytes it was parsed from.
void check_round_trip(const Document& doc) {
    const std::string once   = to_json(doc);
    const auto        parsed = parse_document(once);
    INFO(parse_failure(parsed));
    INFO("json: " << once);
    REQUIRE(parsed.has_value());
    CHECK(to_json(*parsed) == once);
}

// A document exercising every node kind, span kind, and index kind.
Document kitchen_sink() {
    Document doc;

    Synopsis syn;
    syn.code.text = "template <class T>\nclass optional;";
    syn.code.spans.push_back({19, 20, SpanKind::ExposId, "val"});
    syn.code.spans.push_back({25, 33, SpanKind::LibraryIndex, ""});
    doc.nodes.push_back(syn);

    Section sec;
    sec.stable_name = "optional.ctor";
    sec.title       = "Constructors";

    SpecItem item;
    item.decl.signatures.push_back({"constexpr optional() noexcept;", {}});
    item.decl.signatures.push_back({"constexpr optional(nullopt_t) noexcept;", {{10, 17, SpanKind::SeeBelow, ""}}});
    item.decl.signatures.push_back(
        {"using iterator = implementation-defined;", {{17, 39, SpanKind::ImplDefined, ""}}});
    item.decl.index.push_back({IndexKind::Global, "swap", ""});
    item.decl.index.push_back({IndexKind::Constructor, "optional", ""});
    item.decl.index.push_back({IndexKind::Destructor, "optional", ""});
    item.decl.index.push_back({IndexKind::Member, "value", "optional"});
    item.decl.index.push_back({IndexKind::MemberX, "value_type", "optional"});
    item.decl.index.push_back({IndexKind::MemberExpos, "val", "optional"});
    item.decl.index.push_back({IndexKind::Zombie, "auto_ptr", ""});
    item.decl.index.push_back({IndexKind::Misc, "optional", "class template"});

    DescriptionElement mandates;
    mandates.kind = ElementKind::Mandates;
    mandates.paragraphs.push_back({CodeInline{{"is_copy_constructible_v<T>", {{0, 2, SpanKind::Placeholder, "X"}}}},
                                   TextInline{" is "},
                                   CodeInline{{"true", {}}},
                                   RefInline{"optional.general"}});
    mandates.paragraphs.push_back({CodeInline{{"U", {}}}, TextInline{" models "}, ConceptRef{"copyable"}});
    mandates.paragraphs.push_back({TextInline{"A second paragraph."}});
    item.descr.elements.push_back(std::move(mandates));

    DescriptionElement effects;
    effects.kind       = ElementKind::Effects;
    effects.equivalent = EquivalentTo{{"return has_value() ? *this : nullopt;", {{7, 16, SpanKind::Ref, "x.y"}}}};
    item.descr.elements.push_back(std::move(effects));

    sec.children.push_back(std::move(item));
    sec.children.push_back(FreeParagraph{{TextInline{"Free prose."}}});

    Section nested;
    nested.stable_name = "optional.ctor.nested";
    nested.title       = "Nested";
    sec.children.push_back(std::move(nested));

    doc.nodes.push_back(std::move(sec));
    return doc;
}

} // namespace

TEST_CASE("ir_json - round-trip: empty document") { check_round_trip(Document{}); }

TEST_CASE("ir_json - round-trip: every node, span, and index kind") { check_round_trip(kitchen_sink()); }

// How each character is escaped is json_writer's, and
// json/writer.test.cpp pins it; what the IR owes is that a string
// needing escapes comes back out of the parser unchanged.
TEST_CASE("ir_json - round-trip: strings needing escapes") {
    Document doc;
    Synopsis syn;
    // Quote, backslash, newline, tab, carriage return, and a control character
    // the emitter writes as \u00XX.
    syn.code.text = "a \"q\" \\ b\n\tc\r\x01 d";
    doc.nodes.push_back(syn);
    doc.nodes.push_back(FreeParagraph{{TextInline{"unicode: \xE2\x88\x80 \xC2\xA9"}}});
    check_round_trip(doc);
}

TEST_CASE("ir_json - round-trip: an element's itemize, with and without lead-in prose") {
    DescriptionElement mandates;
    mandates.kind = ElementKind::Mandates;
    mandates.itemize =
        Itemize{{{CodeInline{{"A", {}}}, TextInline{" is "}, CodeInline{{"true", {}}}, TextInline{","}},
                 {CodeInline{{"B", {}}}, TextInline{" is "}, CodeInline{{"true", {}}}, TextInline{"."}}}};
    DescriptionElement constraints;
    constraints.kind = ElementKind::Constraints;
    constraints.paragraphs.push_back({TextInline{"All of the following are true:"}});
    constraints.itemize = Itemize{{{TextInline{"one."}}}};

    SpecItem item;
    item.descr.elements = {std::move(mandates), std::move(constraints)};
    Document doc;
    doc.nodes.push_back(std::move(item));
    check_round_trip(doc);

    const auto parsed = parse_document(to_json(doc));
    REQUIRE(parsed.has_value());
    const auto& elements = std::get<SpecItem>(parsed->nodes[0]).descr.elements;
    REQUIRE(elements[0].itemize.has_value());
    CHECK(elements[0].itemize->items.size() == 2);
    REQUIRE(elements[1].itemize.has_value());
    CHECK(elements[1].paragraphs.size() == 1);
}

TEST_CASE("ir_json - round-trip preserves every disposition") {
    Document doc;
    Synopsis syn;
    syn.code.text = "class widget;";
    for (int i = 0; i <= static_cast<int>(Disposition::Undocumented); ++i)
        syn.roster.push_back({"member" + std::to_string(i), static_cast<Disposition>(i), "widget.obs"});
    doc.nodes.push_back(std::move(syn));
    check_round_trip(doc);

    const auto parsed = parse_document(to_json(doc));
    REQUIRE(parsed.has_value());
    const auto& roster = std::get<Synopsis>(parsed->nodes.at(0)).roster;
    REQUIRE(roster.size() == static_cast<std::size_t>(Disposition::Undocumented) + 1);
    CHECK(roster.at(0).disposition == Disposition::Described);
    CHECK(roster.back().disposition == Disposition::Undocumented);
    CHECK(roster.back().section == "widget.obs");
}

// A roster key absent from the JSON is the hand-written-golden case: an entry
// spelling only `name` must default to Described, so omitting the key reports
// nothing rather than manufacturing a coverage error.
TEST_CASE("ir_json - a roster entry with no disposition key defaults to described") {
    const auto parsed = parse_document(R"({"nodes":[{"type":"synopsis","code":{"text":"","spans":[]},)"
                                       R"("roster":[{"name":"f"}]}]})");
    REQUIRE(parsed.has_value());
    const auto& roster = std::get<Synopsis>(parsed->nodes.at(0)).roster;
    REQUIRE(roster.size() == 1);
    CHECK(roster.at(0).name == "f");
    CHECK(roster.at(0).disposition == Disposition::Described);
    CHECK(roster.at(0).section.empty());
    CHECK(roster.at(0).kind == MemberKind::Function);
}

TEST_CASE("ir_json - round-trip preserves a roster entry's member kind") {
    // `kind` is a second axis on a roster entry, independent of
    // `disposition`: private data and a private function differ only here,
    // and design §6 treats them differently.
    Document doc;
    Synopsis syn;
    syn.code.text = "class widget;";
    syn.roster.push_back({"value_", Disposition::Private, "", MemberKind::Data});
    syn.roster.push_back({"helper", Disposition::Private, "", MemberKind::Function});
    doc.nodes.push_back(std::move(syn));
    check_round_trip(doc);

    const auto parsed = parse_document(to_json(doc));
    REQUIRE(parsed.has_value());
    const auto& roster = std::get<Synopsis>(parsed->nodes.at(0)).roster;
    REQUIRE(roster.size() == 2);
    CHECK(roster.at(0).kind == MemberKind::Data);
    CHECK(roster.at(1).kind == MemberKind::Function);
}

TEST_CASE("ir_json - round-trip preserves the document-level validator channels") {
    // Neither list hangs off a node: a surviving namespace
    // qualifier and a body the tool never rendered are facts about the
    // header, so `Document` carries them beside the forest. Both are ignored
    // by every renderer and read only by validate.cpp, which is exactly why
    // no golden's *rendered* output can tell whether they survived emission.
    Document doc;
    doc.nodes.push_back(Synopsis{.name = "widget", .code = {"class widget;", {}}, .roster = {}});
    doc.foreign_namespaces   = {{"detail", "demo::detail"}};
    doc.foreign_declarations = {{"probe_witness", "typeclass_base.hpp"}};
    doc.unextracted_uses     = {{"widget::widget", "hard_reset"}, {"widget::reset", "clone"}};
    check_round_trip(doc);

    const auto parsed = parse_document(to_json(doc));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->foreign_namespaces.size() == 1);
    CHECK(parsed->foreign_namespaces.at(0).qualified == "demo::detail");
    REQUIRE(parsed->foreign_declarations.size() == 1);
    CHECK(parsed->foreign_declarations.at(0).name == "probe_witness");
    CHECK(parsed->foreign_declarations.at(0).header == "typeclass_base.hpp");
    REQUIRE(parsed->unextracted_uses.size() == 2);
    CHECK(parsed->unextracted_uses.at(0).function == "widget::widget");
    CHECK(parsed->unextracted_uses.at(0).member == "hard_reset");
    CHECK(parsed->unextracted_uses.at(1).member == "clone");
}

TEST_CASE("ir_json - round-trip preserves itemdecl entity identities") {
    Document doc;
    SpecItem item;
    item.decl.signatures = {{"template<class T> inline constexpr int alpha_policy = 0;", {}}};
    item.decl.entities   = {"alpha_policy"};
    doc.nodes.push_back(std::move(item));
    check_round_trip(doc);

    const auto parsed = parse_document(to_json(doc));
    REQUIRE(parsed.has_value());
    const auto& entities = std::get<SpecItem>(parsed->nodes.at(0)).decl.entities;
    REQUIRE(entities.size() == 1);
    CHECK(entities.front() == "alpha_policy");
}

TEST_CASE("ir_json - round-trip preserves paper-only entity identities") {
    Document doc;
    doc.paper_entities = std::vector<std::string>{"tag", "tag_t"};

    auto parsed = parse_document(emit_json(doc));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->paper_entities.has_value());
    CHECK(*parsed->paper_entities == std::vector<std::string>{"tag", "tag_t"});
}

TEST_CASE("ir_json - round-trip preserves every element kind") {
    Document doc;
    SpecItem item;
    for (int i = 0; i <= static_cast<int>(ElementKind::Errors); ++i)
        item.descr.elements.push_back({static_cast<ElementKind>(i), {{TextInline{"prose"}}}, {}});
    doc.nodes.push_back(std::move(item));
    check_round_trip(doc);

    const auto parsed = parse_document(to_json(doc));
    INFO(parse_failure(parsed));
    REQUIRE(parsed.has_value());
    const auto& elements = std::get<SpecItem>(parsed->nodes[0]).descr.elements;
    REQUIRE(elements.size() == 13);
    for (int i = 0; i <= static_cast<int>(ElementKind::Errors); ++i)
        CHECK(elements[static_cast<std::size_t>(i)].kind == static_cast<ElementKind>(i));
}

TEST_CASE("ir_json - parse tolerates whitespace, key order, and unknown keys") {
    // Deliberately not the emitter's own layout: a hand-edited golden.
    const std::string json   = R"({
        "nodes": [
            {
                "title": "Constructors",
                "type": "section",
                "note": "unknown keys are ignored",
                "stable": "optional.ctor",
                "children": [ { "type": "synopsis", "code": { "spans": [], "text": "int f();" } } ]
            }
        ]
    })";
    const auto        parsed = parse_document(json);
    INFO(parse_failure(parsed));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->nodes.size() == 1);
    const auto& sec = std::get<Section>(parsed->nodes[0]);
    CHECK(sec.stable_name == "optional.ctor");
    CHECK(sec.title == "Constructors");
    REQUIRE(sec.children.size() == 1);
    CHECK(std::get<Synopsis>(sec.children[0]).code.text == "int f();");
}

TEST_CASE("ir_json - parse rejects malformed input") {
    auto rejects = [](std::string_view json) {
        const auto parsed = parse_document(json);
        INFO("input: " << json);
        REQUIRE_FALSE(parsed.has_value());
        CHECK(!parsed.error().message.empty());
    };

    rejects("");
    rejects("{");
    rejects(R"({"nodes":)");
    rejects(R"({"nodes":[{"type":"bogus"}]})");
    rejects(R"({"nodes":[{"type":"synopsis","code":{"text":"x","spans":[{"kind":"nope"}]}}]})");
    rejects(R"({"nodes":[{"type":"item","descr":{"elements":[{"kind":"nope"}]}}]})");
    rejects(R"({"nodes":[{"type":"para","content":[{"t":"nope"}]}]})");
    rejects(R"({"nodes":[]} trailing)");
    rejects(R"({"nodes":[{"type":"synopsis","code":{"text":"unterminated)");
}

TEST_CASE("ir_json - parse reports a useful error offset") {
    const std::string json   = R"({"nodes":[{"type":"synopsis","code":{"text":"x","spans":[{"kind":"nope"}]}}]})";
    const auto        parsed = parse_document(json);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error().message.find("nope") != std::string::npos);
    CHECK(parsed.error().offset > 0);
    CHECK(parsed.error().offset <= json.size());
}

TEST_CASE("ir_json - item and code parse entry points") {
    SpecItem item;
    item.decl.signatures.push_back({"int f();", {}});
    item.descr.elements.push_back({ElementKind::Returns, {{TextInline{"Nothing."}}}, {}});
    const auto parsed = parse_item(emit_json(item));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->decl.signatures.size() == 1);
    CHECK(parsed->decl.signatures[0].text == "int f();");
    REQUIRE(parsed->descr.elements.size() == 1);
    CHECK(parsed->descr.elements[0].kind == ElementKind::Returns);

    // A non-item node through the item entry point is an error.
    CHECK(!parse_item(R"({"type":"synopsis","code":{"text":"x","spans":[]}})").has_value());

    const auto code = parse_code(emit_json(CodeText{"x", {{0, 1, SpanKind::ExposId, "val"}}}));
    REQUIRE(code.has_value());
    CHECK(code->text == "x");
    REQUIRE(code->spans.size() == 1);
    CHECK(code->spans[0].kind == SpanKind::ExposId);
    CHECK(code->spans[0].payload == "val");
}
