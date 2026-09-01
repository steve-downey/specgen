// tests/beman/specgen/ir.test.cpp                                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/ir.hpp>
#include <beman/specgen/ir.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <string>

using namespace beman::specgen::ir;

TEST_CASE("ir - HeaderIsIdempotent") {
    // Placeholder: verifies header re-inclusion safety and build coherency.
    // This test always passes if the file compiles.
    REQUIRE(true);
}

TEST_CASE("ir - element names round-trip") {
    for (int i = 0; i <= static_cast<int>(ElementKind::Errors); ++i) {
        auto kind = static_cast<ElementKind>(i);
        auto name = element_name(kind);
        CHECK(name != "?");
        auto back = element_from_name(name);
        CHECK(back.has_value());
        CHECK(*back == kind);
    }
    CHECK(!element_from_name("bogus").has_value());
}

TEST_CASE("ir - disposition names round-trip") {
    for (int i = 0; i <= static_cast<int>(Disposition::Undocumented); ++i) {
        auto disposition = static_cast<Disposition>(i);
        auto name        = disposition_name(disposition);
        CHECK(name != "?");
        auto back = disposition_from_name(name);
        CHECK(back.has_value());
        CHECK(*back == disposition);
    }
    CHECK(!disposition_from_name("bogus").has_value());
}

TEST_CASE("ir - canonicalize sorts into structure.specifications order") {
    ItemDescr d;
    d.elements.push_back({ElementKind::Returns, {}, {}});
    d.elements.push_back({ElementKind::Mandates, {}, {}});
    d.elements.push_back({ElementKind::Effects, {}, {}});
    canonicalize(d);
    REQUIRE(d.elements.size() == 3);
    CHECK(d.elements[0].kind == ElementKind::Mandates);
    CHECK(d.elements[1].kind == ElementKind::Effects);
    CHECK(d.elements[2].kind == ElementKind::Returns);
}

TEST_CASE("ir - canonicalize sorts a derived element before an authored one of the same kind") {
    // Design §5.2: "derived conjuncts first, authored prose
    // appended". The authored element is pushed first here (the order
    // frontend.cpp builds them in -- the docblock is lowered before the
    // derivation runs), so this also confirms canonicalize actually
    // reorders rather than merely leaving a pre-sorted input alone.
    DescriptionElement authored;
    authored.kind = ElementKind::Mandates;

    DescriptionElement derived;
    derived.kind    = ElementKind::Mandates;
    derived.derived = true;

    ItemDescr d;
    d.elements.push_back(authored);
    d.elements.push_back(derived);
    canonicalize(d);

    REQUIRE(d.elements.size() == 2);
    CHECK(d.elements[0].derived);
    CHECK_FALSE(d.elements[1].derived);
}

TEST_CASE("ir - JSON emission of a value_or-shaped item") {
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

TEST_CASE("ir - two-dimensional tables round-trip through JSON") {
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

TEST_CASE("ir - document structure emission") {
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

TEST_CASE("ir - JSON string escaping") {
    const std::string json = emit_json(CodeText{"a \"quoted\"\n\tline \\ backslash", {}});
    CHECK(json.find("\\\"quoted\\\"") != std::string::npos);
    CHECK(json.find("\\n") != std::string::npos);
    CHECK(json.find("\\t") != std::string::npos);
    CHECK(json.find("\\\\ backslash") != std::string::npos);
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

TEST_CASE("ir - round-trip: empty document") { check_round_trip(Document{}); }

TEST_CASE("ir - round-trip: every node, span, and index kind") { check_round_trip(kitchen_sink()); }

TEST_CASE("ir - round-trip: strings needing escapes") {
    Document doc;
    Synopsis syn;
    // Quote, backslash, newline, tab, carriage return, and a control character
    // the emitter writes as \u00XX.
    syn.code.text = "a \"q\" \\ b\n\tc\r\x01 d";
    doc.nodes.push_back(syn);
    doc.nodes.push_back(FreeParagraph{{TextInline{"unicode: \xE2\x88\x80 \xC2\xA9"}}});
    check_round_trip(doc);
}

TEST_CASE("ir - round-trip preserves every disposition") {
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
TEST_CASE("ir - a roster entry with no disposition key defaults to described") {
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

TEST_CASE("ir - member kind names round-trip") {
    for (const MemberKind kind : {MemberKind::Function, MemberKind::Data, MemberKind::Alias}) {
        const auto name = member_kind_name(kind);
        CHECK(name != "?");
        const auto back = member_kind_from_name(name);
        REQUIRE(back.has_value());
        CHECK(*back == kind);
    }
    CHECK(!member_kind_from_name("bogus").has_value());
}

TEST_CASE("ir - round-trip preserves a roster entry's member kind") {
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

TEST_CASE("ir - round-trip preserves the document-level validator channels") {
    // Neither list hangs off a node: a surviving namespace
    // qualifier and a body the tool never rendered are facts about the
    // header, so `Document` carries them beside the forest. Both are ignored
    // by every renderer and read only by validate.cpp, which is exactly why
    // no golden's *rendered* output can tell whether they survived emission.
    Document doc;
    doc.nodes.push_back(Synopsis{.name = "widget", .code = {"class widget;", {}}, .roster = {}});
    doc.foreign_namespaces = {{"detail", "demo::detail"}};
    doc.unextracted_uses   = {{"widget::widget", "hard_reset"}, {"widget::reset", "clone"}};
    check_round_trip(doc);

    const auto parsed = parse_document(to_json(doc));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->foreign_namespaces.size() == 1);
    CHECK(parsed->foreign_namespaces.at(0).qualified == "demo::detail");
    REQUIRE(parsed->unextracted_uses.size() == 2);
    CHECK(parsed->unextracted_uses.at(0).function == "widget::widget");
    CHECK(parsed->unextracted_uses.at(0).member == "hard_reset");
    CHECK(parsed->unextracted_uses.at(1).member == "clone");
}

TEST_CASE("ir - round-trip preserves every element kind") {
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

TEST_CASE("ir - parse tolerates whitespace, key order, and unknown keys") {
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

TEST_CASE("ir - parse rejects malformed input") {
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

TEST_CASE("ir - parse reports a useful error offset") {
    const std::string json   = R"({"nodes":[{"type":"synopsis","code":{"text":"x","spans":[{"kind":"nope"}]}}]})";
    const auto        parsed = parse_document(json);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error().message.find("nope") != std::string::npos);
    CHECK(parsed.error().offset > 0);
    CHECK(parsed.error().offset <= json.size());
}

TEST_CASE("ir - item and code parse entry points") {
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

// --- the transitional optional/ParseError* shims -----------------------------
//
// parse_json_document/_item/_code are deprecated wrappers kept so
// out-of-tree callers can migrate on their own schedule; every caller in
// this repository already uses the
// `expected` entry points above. They still need a test, or they rot into a
// second implementation of the same parse. The deprecation warning is
// suppressed here, and nowhere else in the tree: a warning anywhere but this
// block means a caller was missed.
#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

TEST_CASE("ir - the deprecated optional/out-param shims agree with the expected entry points") {
    const std::string json = R"({"nodes":[{"type":"synopsis","code":{"text":"x","spans":[]}}]})";

    const auto shimmed = parse_json_document(json);
    REQUIRE(shimmed.has_value());
    CHECK(to_json(*shimmed) == to_json(*parse_document(json)));

    // On failure: std::nullopt, and the ParseError written through the
    // out-param is the one the expected entry point carries in its error.
    const std::string bad = R"({"nodes":[{"type":"bogus"}]})";
    ParseError        error;
    CHECK_FALSE(parse_json_document(bad, &error).has_value());
    const auto expected_error = parse_document(bad);
    REQUIRE_FALSE(expected_error.has_value());
    CHECK(error.offset == expected_error.error().offset);
    CHECK(error.message == expected_error.error().message);

    // The out-param is optional: a null pointer is not dereferenced.
    CHECK_FALSE(parse_json_document(bad).has_value());

    SpecItem item;
    item.decl.signatures.push_back({"int f();", {}});
    CHECK(parse_json_item(emit_json(item)).has_value());

    CHECK(parse_json_code(emit_json(CodeText{"x", {}})).has_value());
}

#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif
