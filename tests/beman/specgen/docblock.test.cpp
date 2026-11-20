// tests/beman/specgen/docblock.test.cpp                            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/docblock.hpp>
#include <beman/specgen/docblock.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <vector>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <variant>

using namespace beman::specgen::grammar;
namespace ir = beman::specgen::ir;

namespace {

std::size_t                 paragraph_count(const Element& e);
const ProseParagraph&       paragraph(const Element& e, std::size_t n = 0);
std::vector<ProseParagraph> items(const Element& e);
template <class T>
const T* first(const Element& e);

std::size_t paragraph_count(const Element& e) {
    return std::ranges::count_if(e.body,
                                 [](const BodyBlock& block) { return std::holds_alternative<BodyParagraph>(block); });
}
const ProseParagraph& paragraph(const Element& e, std::size_t n) {
    for (const auto& block : e.body)
        if (const auto* p = std::get_if<BodyParagraph>(&block); p != nullptr && n-- == 0)
            return p->text;
    throw std::out_of_range("paragraph");
}
std::vector<ProseParagraph> items(const Element& e) {
    return first<Itemize>(e)->items | std::views::transform(&ListItem::text) | std::ranges::to<std::vector>();
}
template <class T>
const T* first(const Element& e) {
    for (const auto& block : e.body)
        if (const auto* value = std::get_if<T>(&block))
            return value;
    return nullptr;
}

bool has_diag(const ParseResult& r, Severity sev, std::string_view needle) {
    for (const auto& d : r.diags)
        if (d.severity == sev && d.message.find(needle) != std::string::npos)
            return true;
    return false;
}

} // namespace

TEST_CASE("docblock - HeaderIsIdempotent") {
    // Placeholder: verifies header re-inclusion safety and build coherency.
    // This test always passes if the file compiles.
    REQUIRE(true);
}

TEST_CASE("docblock - value_or shape: mandates prose + effects-equiv marker") {
    auto r = parse_docblock("//! \\mandates `is_copy_constructible_v<T>` is `true`.\n"
                            "//! \\effects-equiv\n");
    CHECK(r.ok());
    CHECK(r.block.markers.effects_equiv);
    REQUIRE(r.block.elements.size() == 1);
    const auto& e = r.block.elements[0];
    CHECK(e.kind == ir::ElementKind::Mandates);
    REQUIRE(paragraph_count(e) == 1);
    const auto& p = paragraph(e, 0);
    REQUIRE(p.size() == 4);
    CHECK(std::holds_alternative<InlineCode>(p[0]));
    CHECK(std::get<InlineCode>(p[0]).code == "is_copy_constructible_v<T>");
    CHECK(std::holds_alternative<InlineText>(p[1]));
    CHECK(std::get<InlineText>(p[1]).text == " is ");
    CHECK(std::get<InlineCode>(p[2]).code == "true");
    CHECK(std::get<InlineText>(p[3]).text == ".");
}

TEST_CASE("docblock - multi-paragraph element") {
    auto r = parse_docblock("//! \\remarks First paragraph\n"
                            "//! continues here.\n"
                            "//!\n"
                            "//! Second paragraph.\n");
    CHECK(r.ok());
    REQUIRE(r.block.elements.size() == 1);
    const auto& e = r.block.elements[0];
    REQUIRE(paragraph_count(e) == 2);
    CHECK(std::get<InlineText>(paragraph(e, 0)[0]).text == "First paragraph continues here.");
    CHECK(std::get<InlineText>(paragraph(e, 1)[0]).text == "Second paragraph.");
}

TEST_CASE("docblock - out-of-canonical-order note") {
    auto r = parse_docblock("//! \\returns Something.\n"
                            "//! \\constraints `X` is `true`.\n");
    CHECK(r.ok()); // notes are not errors
    // The note names the element the offending tag came after: with two
    // lines to choose between, a bare "out of canonical order" would not say
    // which one to move.
    CHECK(has_diag(r, Severity::Note, "\\constraints appears after \\returns"));
    CHECK(r.block.elements.size() == 2);
}

TEST_CASE("docblock - the ordering note names the highest-ranked predecessor, not the immediate one") {
    // \effects sits between \constraints and \remarks in canonical order, so
    // the \effects here is out of order with respect to the \remarks two
    // lines above it -- not with respect to the \constraints immediately
    // above it, which is where it belongs. `last_kind` therefore tracks the
    // running maximum's element, not the previous line's.
    auto r = parse_docblock("//! \\remarks Something.\n"
                            "//! \\constraints `X` is `true`.\n"
                            "//! \\effects Does the thing.\n");
    CHECK(r.ok());
    CHECK(has_diag(r, Severity::Note, "\\constraints appears after \\remarks"));
    CHECK(has_diag(r, Severity::Note, "\\effects appears after \\remarks"));
}

TEST_CASE("docblock - unknown tag is an error and its line is retained") {
    auto r = parse_docblock("//! \\bogus stuff\n");
    CHECK(!r.ok());
    CHECK(has_diag(r, Severity::Error, "unknown tag \\bogus; line kept as prose"));
    REQUIRE(r.block.leading.size() == 1);
    const auto& text = std::get<BodyParagraph>(r.block.leading.front()).text;
    REQUIRE(text.size() == 1);
    CHECK(std::get<InlineText>(text.front()).text == "\\bogus stuff");
}

TEST_CASE("docblock - raw LaTeX commands are independent of line wrapping") {
    for (const auto command : {R"(\oldconcept{CopyConstructible})",
                               R"(\placeholder{noex})",
                               R"(\cv{})",
                               R"(\custom[opt]{value})",
                               R"(\custom*{value})",
                               R"(\,)"}) {
        CAPTURE(command);
        const auto joined  = parse_docblock(std::format("//! \\expects Before {} after.\n", command));
        const auto wrapped = parse_docblock(std::format("//! \\expects Before\n//! {} after.\n", command));
        REQUIRE(joined.diags.empty());
        REQUIRE(wrapped.diags.empty());
        REQUIRE(wrapped.block.elements.size() == 1);
        REQUIRE(paragraph_count(wrapped.block.elements.front()) == 1);
        CHECK(std::get<InlineText>(paragraph(wrapped.block.elements.front()).front()).text ==
              std::get<InlineText>(paragraph(joined.block.elements.front()).front()).text);
    }
}

TEST_CASE("docblock - macro-led leading prose and list continuations stay in their blocks") {
    const auto leading = parse_docblock("//! \\placeholder{noex} is a value.\n");
    REQUIRE(leading.diags.empty());
    REQUIRE(leading.block.leading.size() == 1);
    const auto& lead = std::get<BodyParagraph>(leading.block.leading.front()).text;
    REQUIRE(lead.size() == 2);
    CHECK(std::get<InlinePlaceholder>(lead.front()).text == "noex");
    CHECK(std::get<InlineText>(lead.back()).text == " is a value.");

    const auto list = parse_docblock("//! \\remarks Before:\n//! \\item Meets\n"
                                     "//! \\oldconcept{CopyConstructible} requirements.\n//!\n"
                                     "//! \\placeholder{after} follows.\n");
    REQUIRE(list.diags.empty());
    REQUIRE(list.block.elements.size() == 1);
    const auto& element = list.block.elements.front();
    REQUIRE(items(element).size() == 1);
    const auto item = items(element).front();
    REQUIRE(item.size() == 3);
    CHECK(std::get<InlineText>(item.front()).text == "Meets ");
    CHECK(std::get<InlineOldConcept>(item[1]).name == "CopyConstructible");
    CHECK(std::get<InlineText>(item.back()).text == " requirements.");
    CHECK(std::get<InlinePlaceholder>(paragraph(element, 1).front()).text == "after");
    CHECK(std::get<InlineText>(paragraph(element, 1).back()).text == " follows.");
    CHECK(std::get<BodyParagraph>(element.body.back()).continuation);
}

namespace {
bool text_then_placeholder(const ProseParagraph& para, std::string_view text, std::string_view name) {
    return para.size() == 2 && std::get<InlineText>(para.front()).text == text &&
           std::get<InlinePlaceholder>(para.back()).text == name;
}
} // namespace

TEST_CASE("docblock - raw macros continue every table field and resume prose") {
    for (const auto kind : {"libtab2", "lib2dtab2"}) {
        CAPTURE(kind);
        const auto input = std::format(
            "//! \\remarks Table:\n//! \\{}[demo.macros]{{Caption}}\n//! \\placeholder{{caption}}\n"
            "//! \\column First\n//! \\cv{{}} type\n//! \\column Second\n//! \\oldconcept{{Destructible}}\n"
            "//! \\row Head\n//! \\placeholder{{row}}\n//! \\cell Value\n//! \\placeholder{{cell}}\n{}"
            "//! \\end{}\n//! \\placeholder{{resume}} follows.\n",
            kind,
            std::string_view(kind) == "lib2dtab2" ? "//! \\cell Other\n//! \\cv{} type\n" : "",
            kind);
        const auto parsed = parse_docblock(input);
        REQUIRE(parsed.diags.empty());
        REQUIRE(parsed.block.elements.size() == 1);
        const auto& element      = parsed.block.elements.front();
        const auto  check_fields = [](const auto& table) {
            CHECK(text_then_placeholder(table.caption, "Caption ", "caption"));
            CHECK(std::get<InlineText>(table.column1.front()).text == "First ");
            CHECK(std::get<InlinePlaceholder>(table.column1[1]).text == "cv");
            CHECK(std::get<InlineText>(table.column1.back()).text == " type");
            CHECK(std::get<InlineText>(table.column2.front()).text == "Second ");
            CHECK(std::get<InlineOldConcept>(table.column2.back()).name == "Destructible");
            REQUIRE(table.rows.size() == 1);
        };
        if (std::string_view(kind) == "libtab2") {
            const auto* table = first<Table1D>(element);
            REQUIRE(table != nullptr);
            check_fields(*table);
            CHECK(text_then_placeholder(table->rows.front().cell1, "Head ", "row"));
            CHECK(text_then_placeholder(table->rows.front().cell2, "Value ", "cell"));
        } else {
            const auto* table = first<Table2D>(element);
            REQUIRE(table != nullptr);
            check_fields(*table);
            CHECK(text_then_placeholder(table->rows.front().header, "Head ", "row"));
            CHECK(text_then_placeholder(table->rows.front().cell1, "Value ", "cell"));
            CHECK(std::get<InlineText>(table->rows.front().cell2.front()).text == "Other ");
            CHECK(std::get<InlinePlaceholder>(table->rows.front().cell2[1]).text == "cv");
        }
        CHECK(std::get<InlinePlaceholder>(paragraph(element, 1).front()).text == "resume");
        CHECK(std::get<InlineText>(paragraph(element, 1).back()).text == " follows.");
        CHECK(std::get<BodyParagraph>(element.body.back()).continuation);
    }
}

TEST_CASE("docblock - macro-led text continues informative bodies") {
    for (const auto kind : {"note", "example"}) {
        CAPTURE(kind);
        const auto parsed = parse_docblock(
            std::format("//! \\{} Before\n//! \\oldconcept{{Destructible}} requirements.\n//! \\end{}\n", kind, kind));
        REQUIRE(parsed.diags.empty());
        REQUIRE(parsed.block.leading.size() == 1);
        const auto& block = parsed.block.leading.front();
        const auto& body =
            std::string_view(kind) == "note" ? std::get<Note>(block).body : std::get<Example>(block).body;
        REQUIRE(body.size() == 1);
        const auto& text = std::get<BodyParagraph>(body.front()).text;
        REQUIRE(text.size() == 3);
        CHECK(std::get<InlineText>(text.front()).text == "Before ");
        CHECK(std::get<InlineOldConcept>(text[1]).name == "Destructible");
        CHECK(std::get<InlineText>(text.back()).text == " requirements.");
    }
}

TEST_CASE("docblock - an escaped ampersand, percent or hash in prose is the character") {
    // Either spelling is right in every backend: the LaTeX one escapes the
    // character again (issue #352).
    const auto parsed = parse_docblock("//! R\\&D and R&D, 50\\% and 50%, \\#1 and #1, `a\\&b`, \\{.\n");
    REQUIRE(parsed.diags.empty());
    const auto& para = std::get<BodyParagraph>(parsed.block.leading.front()).text;
    REQUIRE(para.size() == 3);
    CHECK(std::get<InlineText>(para[0]).text == "R&D and R&D, 50% and 50%, #1 and #1, ");
    CHECK(std::get<InlineCode>(para[1]).code == "a\\&b");
    CHECK(std::get<InlineText>(para[2]).text == ", \\{.");
}

TEST_CASE("docblock - unknown commands retain text in paragraphs lists and tables") {
    const auto prose = parse_docblock("//! \\effects Before\n//! \\effect kept `code`.\n//! After.\n");
    CHECK(has_diag(prose, Severity::Error, "unknown tag \\effect; line kept as prose"));
    REQUIRE(prose.block.elements.size() == 1);
    const auto& text = paragraph(prose.block.elements.front());
    REQUIRE(text.size() == 3);
    CHECK(std::get<InlineText>(text.front()).text == "Before \\effect kept ");
    CHECK(std::get<InlineCode>(text[1]).code == "code");
    CHECK(std::get<InlineText>(text.back()).text == ". After.");

    const auto list = parse_docblock("//! \\effects List:\n//! \\item Before\n//! \\effect kept.\n");
    CHECK(has_diag(list, Severity::Error, "unknown tag \\effect"));
    REQUIRE(list.block.elements.size() == 1);
    REQUIRE(items(list.block.elements.front()).size() == 1);
    CHECK(std::get<InlineText>(items(list.block.elements.front()).front().front()).text == "Before \\effect kept.");

    const auto table = parse_docblock("//! \\remarks Table:\n//! \\libtab2[demo.unknown]{Caption}\n"
                                      "//! \\column A\n//! \\column B\n//! \\row Key\n//! \\cell Before\n"
                                      "//! \\effect kept.\n//! \\endlibtab2\n");
    CHECK(has_diag(table, Severity::Error, "unknown tag \\effect"));
    REQUIRE(table.block.elements.size() == 1);
    const auto* flat = first<Table1D>(table.block.elements.front());
    REQUIRE(flat != nullptr);
    REQUIRE(flat->rows.size() == 1);
    CHECK(std::get<InlineText>(flat->rows.front().cell2.front()).text == "Before \\effect kept.");
}

TEST_CASE("docblock - exact payloads bypass command classification") {
    constexpr auto payload = "\\oldconcept{Destructible}\n\\effect kept.";
    const auto     code    = parse_docblock("//! \\codeblock\n//! \\oldconcept{Destructible}\n"
                                            "//! \\effect kept.\n//! \\endcodeblock\n");
    REQUIRE(code.diags.empty());
    REQUIRE(code.block.leading.size() == 1);
    CHECK(std::get<AuthoredCode>(code.block.leading.front()).code.text == payload);
    for (const auto kind : {"verbatim-synopsis", "verbatim-itemdecl"}) {
        CAPTURE(kind);
        const auto parsed =
            parse_docblock(std::format("//! \\{}\n//! \\oldconcept{{Destructible}}\n//! \\effect kept.\n", kind));
        REQUIRE(parsed.diags.empty());
        CHECK((std::string_view(kind) == "verbatim-synopsis" ? parsed.block.verbatim_synopsis
                                                             : parsed.block.verbatim_itemdecl) == payload);
    }
}

TEST_CASE("docblock - recommended follows errors and reports ordering and duplicates") {
    const auto ordered = parse_docblock("//! \\errors An error condition.\n"
                                        "//! \\recommended Implementations should avoid allocation.\n");
    REQUIRE(ordered.ok());
    REQUIRE(ordered.diags.empty());
    REQUIRE(ordered.block.elements.size() == 2);
    CHECK(ordered.block.elements.back().kind == ir::ElementKind::Recommended);

    const auto unordered = parse_docblock("//! \\recommended Avoid allocation.\n"
                                          "//! \\remarks An observation.\n"
                                          "//! \\recommended Avoid another allocation.\n");
    CHECK(unordered.ok());
    CHECK(has_diag(unordered, Severity::Note, "\\remarks appears after \\recommended"));
    CHECK(has_diag(unordered, Severity::Warning, "duplicate"));
}

TEST_CASE("docblock - expos with explicit name") {
    auto r = parse_docblock("//! \\expos(val)\n");
    CHECK(r.ok());
    CHECK(r.block.markers.expos);
    REQUIRE(r.block.markers.expos_name.has_value());
    CHECK(*r.block.markers.expos_name == "val");
}

TEST_CASE("docblock - bare expos") {
    auto r = parse_docblock("//! \\expos\n");
    CHECK(r.ok());
    CHECK(r.block.markers.expos);
    CHECK(!r.block.markers.expos_name.has_value());
}

TEST_CASE("docblock - effects and effects-equiv conflict") {
    auto r = parse_docblock("//! \\effects Does things.\n"
                            "//! \\effects-equiv\n");
    CHECK(!r.ok());
    CHECK(has_diag(r, Severity::Error, "mutually exclusive"));
}

TEST_CASE("docblock - prose before first tag is a leading description") {
    const auto r = parse_docblock("//! Leading prose.\n//! \\effects Fine.\n");
    CHECK(r.ok());
    CHECK(r.diags.empty());
    REQUIRE(r.block.leading.size() == 1);
    const auto& leading = std::get<BodyParagraph>(r.block.leading.front());
    REQUIRE(leading.text.size() == 1);
    CHECK(std::get<InlineText>(leading.text.front()).text == "Leading prose.");
    REQUIRE(r.block.elements.size() == 1);
    CHECK(r.block.elements.front().kind == ir::ElementKind::Effects);
}

TEST_CASE("docblock - lone also marker") {
    auto r = parse_docblock("//! \\also\n");
    CHECK(r.ok());
    CHECK(r.block.markers.also);
    CHECK(r.block.elements.empty());
}

TEST_CASE("docblock - named group and also markers") {
    auto primary = parse_docblock("//! \\group left\n//! \\returns A value.\n");
    REQUIRE(primary.ok());
    REQUIRE(primary.block.markers.group_id.has_value());
    CHECK(*primary.block.markers.group_id == "left");

    auto follower = parse_docblock("//! \\also left\n");
    REQUIRE(follower.ok());
    CHECK(follower.block.markers.also);
    REQUIRE(follower.block.markers.also_target.has_value());
    CHECK(*follower.block.markers.also_target == "left");
}

TEST_CASE("docblock - group requires an id and conflicts with also") {
    auto missing = parse_docblock("//! \\group\n");
    CHECK(!missing.ok());
    CHECK(has_diag(missing, Severity::Error, "\\group requires an id"));

    auto conflict = parse_docblock("//! \\group left\n//! \\also left\n");
    CHECK(!conflict.ok());
    CHECK(has_diag(conflict, Severity::Error, "\\group and \\also are mutually exclusive"));
}

TEST_CASE("docblock - at anchor") {
    auto r = parse_docblock("//! \\at optional.observe\n");
    CHECK(r.ok());
    REQUIRE(r.block.markers.at_anchor.has_value());
    CHECK(*r.block.markers.at_anchor == "optional.observe");

    auto bad = parse_docblock("//! \\at\n");
    CHECK(!bad.ok());
    CHECK(has_diag(bad, Severity::Error, "\\at requires an anchor"));
}

TEST_CASE("docblock - block comment form") {
    auto r = parse_docblock("/*!\n"
                            " * \\omit\n"
                            " */\n");
    CHECK(r.ok());
    CHECK(r.block.markers.omit);
}

TEST_CASE("docblock - unterminated backtick is an error") {
    auto r = parse_docblock("//! \\effects Calls `foo(\n");
    CHECK(!r.ok());
    CHECK(has_diag(r, Severity::Error, "unterminated"));
}

TEST_CASE("docblock - authored items belong to the current element") {
    auto r = parse_docblock("//! \\constraints All of the following are true:\n"
                            "//! \\item `A` is `true`,\n"
                            "//!\n"
                            "//! \\item `B` is\n"
                            "//! `false`.\n");
    CHECK(r.ok());
    REQUIRE(r.block.elements.size() == 1);
    const Element& constraints = r.block.elements.front();
    REQUIRE(paragraph_count(constraints) == 1);
    REQUIRE(items(constraints).size() == 2);
    CHECK(std::get<InlineText>(paragraph(constraints).front()).text == "All of the following are true:");
    CHECK(std::get<InlineCode>(items(constraints).at(0).at(0)).code == "A");
    CHECK(std::get<InlineText>(items(constraints).at(0).back()).text == ",");
    CHECK(std::get<InlineCode>(items(constraints).at(1).at(2)).code == "false");
    CHECK(std::get<InlineText>(items(constraints).at(1).back()).text == ".");
}

TEST_CASE("docblock - authored item diagnostics and resumed prose") {
    const auto before = parse_docblock("//! \\item orphan\n");
    CHECK(before.ok());
    REQUIRE(before.block.leading.size() == 1);
    CHECK(std::get<Itemize>(before.block.leading.front()).items.size() == 1);

    const auto empty = parse_docblock("//! \\constraints\n//! \\item\n//! \\effects Fine.\n");
    CHECK(!empty.ok());
    CHECK(has_diag(empty, Severity::Error, "\\item requires content"));

    const auto after = parse_docblock("//! \\constraints\n//! \\item One.\n//!\n//! Stray prose.\n");
    CHECK(after.ok());
    REQUIRE(after.block.elements.front().body.size() == 2);
    CHECK(std::get<BodyParagraph>(after.block.elements.front().body.back()).continuation);

    const auto after_marker =
        parse_docblock("//! \\constraints\n//! \\item One.\n//! \\freestanding\n//! Stray prose.\n");
    CHECK(after_marker.ok());
    CHECK(std::get<BodyParagraph>(after_marker.block.elements.front().body.back()).continuation);
}

TEST_CASE("docblock - the draft's prose macros the importer reads are inline kinds here too") {
    // A sentence typed in a header and the same sentence inherited from the
    // draft have to be the same wording (issue #316).
    auto r = parse_docblock("//! \\remarks Within subclause \\ref{func.wrap.ref}, a type that satisfies\n"
                            "//! \\exposconcept{constexpr-param} models \\libconcept{copyable} for some\n"
                            "//! \\cvqual{cv2}, as \\tref{gadget.table} shows; `\\ref{literal}` is code.\n");
    CHECK(r.ok());
    REQUIRE(r.block.elements.size() == 1);
    const ProseParagraph& para = paragraph(r.block.elements.front());
    REQUIRE(para.size() == 13);
    CHECK(std::get<InlineText>(para.at(0)).text == "Within subclause ");
    CHECK(std::get<InlineRef>(para.at(1)).stable_name == "func.wrap.ref");
    CHECK(std::get<InlineRef>(para.at(1)).bare);
    CHECK(std::get<InlineConcept>(para.at(3)).name == "constexpr-param");
    CHECK(std::get<InlineConcept>(para.at(3)).exposition);
    CHECK(std::get<InlineConcept>(para.at(5)).name == "copyable");
    CHECK_FALSE(std::get<InlineConcept>(para.at(5)).exposition);
    CHECK(std::get<InlinePlaceholder>(para.at(7)).text == "cv2");
    CHECK(std::get<InlinePlaceholder>(para.at(7)).cv_qualifier);
    CHECK(std::get<InlineTableRef>(para.at(9)).stable_name == "gadget.table");
    CHECK(std::get<InlineCode>(para.at(11)).code == "\\ref{literal}");
    CHECK(std::get<InlineText>(para.at(12)).text == " is code.");

    // \iref keeps its parentheses, and the plain placeholder is not a cv-qualifier.
    auto plain = parse_docblock("//! \\remarks As \\placeholder{p} is\\iref{x}.\n");
    CHECK(plain.ok());
    const ProseParagraph& text = paragraph(plain.block.elements.front());
    CHECK_FALSE(std::get<InlinePlaceholder>(text.at(1)).cv_qualifier);
    CHECK_FALSE(std::get<InlineRef>(text.at(3)).bare);

    // Each needs its argument, and a macro the grammar does not read stays text.
    for (const char* macro :
         {"ref", "tref", "libconcept", "exposconcept", "cvqual", "term", "defn", "Fundescx", "impldef"}) {
        CAPTURE(macro);
        const auto empty = parse_docblock("//! \\remarks A \\" + std::string(macro) + "{} here.\n");
        REQUIRE(empty.diags.size() == 1);
        CHECK(empty.diags.front().message == "\\" + std::string(macro) + " requires an argument");
        const auto open = parse_docblock("//! \\remarks A \\" + std::string(macro) + "{x here.\n");
        REQUIRE(open.diags.size() == 1);
        CHECK(open.diags.front().message == "unterminated \\" + std::string(macro) + " macro");
    }
    auto other = parse_docblock("//! \\remarks A \\textbf{thing} and \\pageref{x}.\n");
    CHECK(other.ok());
    CHECK(paragraph(other.block.elements.front()).size() == 1);
}

TEST_CASE("docblock - a term and a fixed phrase are inline kinds, as the importer reads them") {
    // Issue #355.
    auto r = parse_docblock("//! \\remarks A \\term{valid `T`}, \\defn{d}, the \\Fundescx{Effects}, an "
                            "\\impldef{what} \\ntbs{} and \\ntmbs{}.\n");
    CHECK(r.ok());
    const ProseParagraph& para = paragraph(r.block.elements.front());
    REQUIRE(para.size() == 13);
    CHECK(std::get<InlineTerm>(para.at(1)).body == "valid `T`");
    CHECK(std::get<InlineTerm>(para.at(1)).role == beman::specgen::ir::TermRole::Term);
    CHECK(std::get<InlineTerm>(para.at(3)).role == beman::specgen::ir::TermRole::Definition);
    CHECK(std::get<InlineTerm>(para.at(5)).role == beman::specgen::ir::TermRole::ElementName);
    CHECK(std::get<InlinePhrase>(para.at(7)).phrase == beman::specgen::ir::Phrase::ImplementationDefined);
    CHECK(std::get<InlinePhrase>(para.at(7)).index == "what");
    CHECK(std::get<InlinePhrase>(para.at(9)).phrase == beman::specgen::ir::Phrase::Ntbs);
    CHECK(std::get<InlinePhrase>(para.at(11)).phrase == beman::specgen::ir::Phrase::Ntmbs);

    const auto argued = parse_docblock("//! \\remarks An \\ntbs{x}.\n");
    REQUIRE(argued.diags.size() == 1);
    CHECK(argued.diags.front().message == "\\ntbs takes no argument");
}

TEST_CASE("docblock - iref is prose markup outside backticks") {
    auto r = parse_docblock("//! \\remarks \\iref{class.union.general} follows `\\iref{literal}`.\n");
    CHECK(r.ok());
    REQUIRE(r.block.elements.size() == 1);
    const ProseParagraph& para = paragraph(r.block.elements.front());
    REQUIRE(para.size() == 4);
    CHECK(std::get<InlineRef>(para.at(0)).stable_name == "class.union.general");
    CHECK(std::get<InlineText>(para.at(1)).text == " follows ");
    CHECK(std::get<InlineCode>(para.at(2)).code == "\\iref{literal}");
    CHECK(std::get<InlineText>(para.at(3)).text == ".");
}

TEST_CASE("docblock - malformed iref is diagnosed") {
    const auto empty = parse_docblock("//! \\remarks See \\iref{}.\n");
    CHECK(!empty.ok());
    CHECK(has_diag(empty, Severity::Error, "requires a stable name"));

    const auto unterminated = parse_docblock("//! \\remarks See \\iref{class.union.general\n");
    CHECK(!unterminated.ok());
    CHECK(has_diag(unterminated, Severity::Error, "unterminated \\iref"));
}

TEST_CASE("docblock - an authored two-dimensional table preserves table content") {
    auto r = parse_docblock("//! \\effects See the following table.\n"
                            "//! \\lib2dtab2[optional.assign.copy]{`optional::operator=(const optional&)` effects}\n"
                            "//! continued caption\n"
                            "//! \\column `*this` contains a value\n"
                            "//! \\column `*this` does not contain a value\n"
                            "//! \\row `rhs` contains a value\n"
                            "//! \\cell assigns the contained value,\n"
                            "//! with continuation\n"
                            "//! \\cell direct-non-list-initializes the contained value.\n"
                            "//! \\endlib2dtab2\n");
    CHECK(r.ok());
    REQUIRE(r.block.elements.size() == 1);
    const auto& table = first<Table2D>(r.block.elements.front());
    REQUIRE(table != nullptr);
    CHECK(table->stable_name == "optional.assign.copy");
    CHECK(std::get<InlineText>(table->caption.back()).text == " effects continued caption");
    CHECK(std::get<InlineCode>(table->column1.front()).code == "*this");
    REQUIRE(table->rows.size() == 1);
    CHECK(std::get<InlineText>(table->rows.front().cell1.front()).text ==
          "assigns the contained value, with continuation");
}

TEST_CASE("docblock - malformed two-dimensional table structure is diagnosed") {
    const auto orphan = parse_docblock("//! \\lib2dtab2[x]{caption}\n//! \\endlib2dtab2\n");
    CHECK(!orphan.ok());
    CHECK(has_diag(orphan, Severity::Error, "requires a preceding element"));

    const auto nested = parse_docblock("//! \\effects\n"
                                       "//! \\lib2dtab2[x]{caption}\n"
                                       "//! \\lib2dtab2[y]{nested}\n"
                                       "//! \\row row\n"
                                       "//! \\cell one\n"
                                       "//! \\endlib2dtab2\n");
    CHECK(!nested.ok());
    CHECK(has_diag(nested, Severity::Error, "nested"));
    CHECK(has_diag(nested, Severity::Error, "two preceding"));
    CHECK(has_diag(nested, Severity::Error, "exactly two \\column"));
    CHECK(has_diag(nested, Severity::Error, "exactly two \\cell"));

    const auto missing_end = parse_docblock("//! \\effects\n"
                                            "//! \\lib2dtab2[x]{caption}\n"
                                            "//! \\column one\n"
                                            "//! \\column two\n"
                                            "//! \\row row\n"
                                            "//! \\cell one\n"
                                            "//! \\cell two\n");
    CHECK(!missing_end.ok());
    CHECK(has_diag(missing_end, Severity::Error, "missing \\endlib2dtab2"));

    const auto order = parse_docblock("//! \\effects\n"
                                      "//! \\lib2dtab2[x]{caption}\n"
                                      "//! \\cell before row\n"
                                      "//! \\column one\n"
                                      "//! \\column two\n"
                                      "//! \\row row\n"
                                      "//! \\cell one\n"
                                      "//! \\cell two\n"
                                      "//! \\column late\n"
                                      "//! \\endlib2dtab2\n");
    CHECK(!order.ok());
    CHECK(has_diag(order, Severity::Error, "requires a preceding \\row"));
    CHECK(has_diag(order, Severity::Error, "must precede every \\row"));

    const auto trailing = parse_docblock("//! \\effects\n"
                                         "//! \\lib2dtab2[x]{caption}\n"
                                         "//! \\column one\n"
                                         "//! \\column two\n"
                                         "//! \\row row\n"
                                         "//! \\cell one\n"
                                         "//! \\cell two\n"
                                         "//! \\endlib2dtab2\n"
                                         "//! more prose\n");
    CHECK(trailing.ok());
    CHECK(std::get<BodyParagraph>(trailing.block.elements.front().body.back()).continuation);

    const auto duplicate = parse_docblock("//! \\effects\n"
                                          "//! \\lib2dtab2[x]{first}\n"
                                          "//! \\column one\n//! \\column two\n"
                                          "//! \\row row\n//! \\cell one\n//! \\cell two\n"
                                          "//! \\endlib2dtab2\n"
                                          "//! \\effects\n"
                                          "//! \\lib2dtab2[y]{second}\n"
                                          "//! \\column one\n//! \\column two\n"
                                          "//! \\row row\n//! \\cell one\n//! \\cell two\n"
                                          "//! \\endlib2dtab2\n");
    CHECK(duplicate.ok());
    CHECK(has_diag(duplicate, Severity::Warning, "duplicate"));
}

TEST_CASE("docblock - an authored flat two-column table preserves table content") {
    auto r = parse_docblock("//! \\remarks The enumerators have the meanings in the table.\n"
                            "//! \\libtab2[demo.errors.tab]{Enum class `whatwg_error`}\n"
                            "//! \\column Constant\n"
                            "//! \\column Meaning\n"
                            "//! \\row `invalid_byte`\n"
                            "//! \\cell the input holds a byte the encoding does not allow,\n"
                            "//! in that position.\n"
                            "//! \\row `truncated_sequence`\n"
                            "//! \\cell the input ends in the middle of a sequence.\n"
                            "//! \\endlibtab2\n");
    CHECK(r.ok());
    REQUIRE(r.block.elements.size() == 1);
    const auto& table = first<Table1D>(r.block.elements.front());
    REQUIRE(table != nullptr);
    CHECK(table->stable_name == "demo.errors.tab");
    CHECK(std::get<InlineText>(table->column1.front()).text == "Constant");
    CHECK(std::get<InlineText>(table->column2.front()).text == "Meaning");
    REQUIRE(table->rows.size() == 2);
    CHECK(std::get<InlineCode>(table->rows.front().cell1.front()).code == "invalid_byte");
    CHECK(std::get<InlineText>(table->rows.front().cell2.front()).text ==
          "the input holds a byte the encoding does not allow, in that position.");
    CHECK(std::get<InlineCode>(table->rows.back().cell1.front()).code == "truncated_sequence");
}

TEST_CASE("docblock - malformed flat two-column table structure is diagnosed") {
    const auto orphan = parse_docblock("//! \\libtab2[x]{caption}\n//! \\endlibtab2\n");
    CHECK(!orphan.ok());
    CHECK(has_diag(orphan, Severity::Error, "requires a preceding element"));

    const auto nested = parse_docblock("//! \\remarks\n"
                                       "//! \\libtab2[x]{caption}\n"
                                       "//! \\libtab2[y]{nested}\n"
                                       "//! \\row row\n"
                                       "//! \\endlibtab2\n");
    CHECK(!nested.ok());
    CHECK(has_diag(nested, Severity::Error, "nested"));
    CHECK(has_diag(nested, Severity::Error, "two preceding"));
    CHECK(has_diag(nested, Severity::Error, "exactly two \\column"));
    CHECK(has_diag(nested, Severity::Error, "exactly one \\cell"));

    const auto missing_end = parse_docblock("//! \\remarks\n"
                                            "//! \\libtab2[x]{caption}\n"
                                            "//! \\column one\n"
                                            "//! \\column two\n"
                                            "//! \\row row\n"
                                            "//! \\cell one\n");
    CHECK(!missing_end.ok());
    CHECK(has_diag(missing_end, Severity::Error, "missing \\endlibtab2"));

    const auto too_many_cells = parse_docblock("//! \\remarks\n"
                                               "//! \\libtab2[x]{caption}\n"
                                               "//! \\column one\n"
                                               "//! \\column two\n"
                                               "//! \\row row\n"
                                               "//! \\cell one\n"
                                               "//! \\cell two\n"
                                               "//! \\endlibtab2\n");
    CHECK(!too_many_cells.ok());
    CHECK(has_diag(too_many_cells, Severity::Error, "exactly one \\cell"));

    const auto trailing = parse_docblock("//! \\remarks\n"
                                         "//! \\libtab2[x]{caption}\n"
                                         "//! \\column one\n"
                                         "//! \\column two\n"
                                         "//! \\row row\n"
                                         "//! \\cell one\n"
                                         "//! \\endlibtab2\n"
                                         "//! more prose\n");
    CHECK(trailing.ok());
    CHECK(std::get<BodyParagraph>(trailing.block.elements.front().body.back()).continuation);

    const auto duplicate = parse_docblock("//! \\remarks\n"
                                          "//! \\libtab2[x]{first}\n"
                                          "//! \\column one\n//! \\column two\n"
                                          "//! \\row row\n//! \\cell one\n"
                                          "//! \\endlibtab2\n"
                                          "//! \\remarks\n"
                                          "//! \\libtab2[y]{second}\n"
                                          "//! \\column one\n//! \\column two\n"
                                          "//! \\row row\n//! \\cell one\n"
                                          "//! \\endlibtab2\n");
    CHECK(duplicate.ok());
    CHECK(has_diag(duplicate, Severity::Warning, "duplicate"));

    // The two table kinds are independent constructs: \lib2dtab2 and \libtab2
    // do not collide with each other's "one table per element kind" check.
    const auto mixed = parse_docblock("//! \\remarks\n"
                                      "//! \\lib2dtab2[x]{2d}\n"
                                      "//! \\column one\n//! \\column two\n"
                                      "//! \\row row\n//! \\cell one\n//! \\cell two\n"
                                      "//! \\endlib2dtab2\n"
                                      "//! \\remarks\n"
                                      "//! \\libtab2[y]{flat}\n"
                                      "//! \\column one\n//! \\column two\n"
                                      "//! \\row row\n//! \\cell one\n"
                                      "//! \\endlibtab2\n");
    CHECK(mixed.ok());
}

TEST_CASE("docblock - duplicate element warns but keeps both") {
    auto r = parse_docblock("//! \\remarks One.\n"
                            "//! \\remarks Two.\n");
    CHECK(r.ok()); // warning, not error
    CHECK(has_diag(r, Severity::Warning, "duplicate \\remarks"));
    CHECK(r.block.elements.size() == 2);
}

TEST_CASE("docblock - seebelow accepts conditional-specifier targets only") {
    const auto bare = parse_docblock("//! \\seebelow\n");
    CHECK(bare.ok());
    CHECK(bare.block.markers.seebelow);
    CHECK(!bare.block.markers.seebelow_target.has_value());

    for (const std::string_view target : {"noexcept", "explicit"}) {
        const auto conditional = parse_docblock(std::format("//! \\seebelow {}\n", target));
        CHECK(conditional.ok());
        REQUIRE(conditional.block.markers.seebelow_target.has_value());
        CHECK(*conditional.block.markers.seebelow_target == target);
    }

    const auto bad = parse_docblock("//! \\seebelow result\n");
    CHECK(!bad.ok());
    CHECK(has_diag(bad, Severity::Error, "unknown \\seebelow target 'result'"));
}

TEST_CASE("docblock - impdef is a flag marker") {
    const auto parsed = parse_docblock("//! \\impdef\n");
    CHECK(parsed.ok());
    CHECK(parsed.block.markers.impdef);
}

TEST_CASE("docblock - impdef and seebelow conflict") {
    const auto parsed = parse_docblock("//! \\impdef\n//! \\seebelow\n");
    CHECK(has_diag(parsed, Severity::Error, "\\impdef and \\seebelow are mutually exclusive"));
}

TEST_CASE("docblock - elsewhere is exclusive with other disposition markers") {
    CHECK(has_diag(parse_docblock("//! \\elsewhere\n//! \\omit\n"),
                   Severity::Error,
                   "\\elsewhere and \\omit are mutually exclusive"));
    CHECK(has_diag(parse_docblock("//! \\elsewhere\n//! \\merge\n"),
                   Severity::Error,
                   "\\elsewhere and \\merge are mutually exclusive"));
    CHECK(has_diag(parse_docblock("//! \\elsewhere\n//! \\expos\n"),
                   Severity::Error,
                   "\\elsewhere and \\expos are mutually exclusive"));
}

TEST_CASE("docblock - verbatim synopsis consumes the terminal payload without parsing it") {
    const auto parsed = parse_docblock("//! \\verbatim-synopsis\n"
                                       "//! namespace std {\n"
                                       "//!   template<class T> struct hash<optional<T>>;\n"
                                       "//! }\n");

    REQUIRE(parsed.ok());
    CHECK(parsed.block.markers.verbatim_synopsis);
    REQUIRE(parsed.block.verbatim_synopsis.has_value());
    CHECK(*parsed.block.verbatim_synopsis == "namespace std {\n  template<class T> struct hash<optional<T>>;\n}");
}

TEST_CASE("docblock - verbatim itemdecl preserves multiple declarations as one terminal payload") {
    const auto parsed = parse_docblock("//! \\remarks The type represents the no-value state.\n"
                                       "//! \\verbatim-itemdecl\n"
                                       "//! struct nullopt_t {@\\seebelow@};\n"
                                       "//!\n"
                                       "//! inline constexpr nullopt_t nullopt(@\\unspec@);\n");

    REQUIRE(parsed.ok());
    REQUIRE(parsed.block.elements.size() == 1);
    CHECK(parsed.block.elements.front().kind == ir::ElementKind::Remarks);
    REQUIRE(parsed.block.verbatim_itemdecl.has_value());
    CHECK(*parsed.block.verbatim_itemdecl == "struct nullopt_t {@\\seebelow@};\n\n"
                                             "inline constexpr nullopt_t nullopt(@\\unspec@);");
}

TEST_CASE("docblock - verbatim itemdecl payload is not parsed as markup") {
    const auto parsed = parse_docblock("//! \\verbatim-itemdecl\n"
                                       "//! \\not-a-specgen-tag\n");

    REQUIRE(parsed.ok());
    REQUIRE(parsed.block.verbatim_itemdecl.has_value());
    CHECK(*parsed.block.verbatim_itemdecl == "\\not-a-specgen-tag");
}

// Decision marker-registry: walk the registry itself, so a marker added
// to `kMarkers` without the matching parsing wiring (or vice versa) fails
// here instead of silently drifting. Each entry is exercised with the
// argument shape its arity demands, then checked back through the very
// `flag`/`set_arg` the table lists for it — never a second, hand-written
// enumeration of what each marker "should" do.
TEST_CASE("docblock - marker registry: every entry parses and round-trips") {
    for (const MarkerInfo& entry : kMarkers) {
        CAPTURE(entry.spelling);

        std::string input = "\\" + std::string(entry.spelling);
        switch (entry.arity) {
        case MarkerArity::Flag:
            break;
        case MarkerArity::ParenOptional:
            input += "(x)";
            break;
        case MarkerArity::RestRequired:
            input += " x";
            break;
        case MarkerArity::RestOptional:
            // Each RestOptional marker takes `noexcept`: a \seebelow and an
            // \omit target, and an \also group id.
            input += " noexcept";
            break;
        }
        input += "\n";
        if (entry.spelling == "covers")
            input = "\\constraints\n" + input;

        auto r = parse_docblock(input);
        CHECK(!has_diag(r, Severity::Error, "unknown tag"));
        CHECK(r.ok());

        // A targeted \omit keeps the declaration, so it is not the flag
        // (issue #385).
        if (entry.flag != nullptr && entry.spelling != "omit")
            CHECK(r.block.markers.*(entry.flag));

        if (entry.set_arg != nullptr)
            // The argument text was already stored while parsing `input`
            // above; calling the setter again reports that fact back.
            CHECK(entry.set_arg(r.block.markers, "probe"));

        if (entry.arity == MarkerArity::RestRequired)
            CHECK(!entry.missing_arg_error.empty());
    }
}

TEST_CASE("docblock - specified is a flag marker") {
    const auto parsed = parse_docblock("//! \\specified\n");
    CHECK(parsed.ok());
    CHECK(parsed.block.markers.specified);
    CHECK_FALSE(parse_docblock("//! \\omit\n").block.markers.specified);
}

TEST_CASE("docblock - a slash that is not a line decoration passes through") {
    CHECK(strip_comment_decorations("//! \\effects A path follows.\n/path\n/*! another comment */") ==
          "\\effects A path follows.\n/path\n/*! another comment */");
}

TEST_CASE("docblock - leading prose preserves lists and resumed paragraphs") {
    const auto parsed = parse_docblock("//! Choose one:\n//! \\item First, or\n//! \\item Second.\n//!\n"
                                       "//! Resume this paragraph.\n//!\n//! A new paragraph.\n"
                                       "//! \\item Another list.\n//! \\effects Performs the operation.\n");
    REQUIRE(parsed.ok());
    REQUIRE(parsed.block.leading.size() == 5);
    CHECK(std::get<Itemize>(parsed.block.leading[1]).items.size() == 2);
    CHECK(std::get<BodyParagraph>(parsed.block.leading[2]).continuation);
    CHECK_FALSE(std::get<BodyParagraph>(parsed.block.leading[3]).continuation);
    CHECK(std::get<Itemize>(parsed.block.leading[4]).items.size() == 1);
    CHECK(parsed.block.elements.size() == 1);
}

TEST_CASE("docblock - two blank lines after a list start a new paragraph") {
    for (const auto prefix : {"", "\\remarks "}) {
        for (const int count : {1, 2, 3}) {
            CAPTURE(prefix, count);
            std::string blanks;
            for (int i = 0; i < count; ++i)
                blanks += "//!\n";
            const auto parsed = parse_docblock(std::format(
                "//! {}Choose one:\n//! \\item First.\n{}//! Choose again:\n//! \\item Second.\n", prefix, blanks));
            REQUIRE(parsed.diags.empty());
            const auto& body =
                parsed.block.elements.empty() ? parsed.block.leading : parsed.block.elements.front().body;
            REQUIRE(body.size() == 4);
            CHECK(std::get<Itemize>(body[1]).items.size() == 1);
            CHECK(std::get<BodyParagraph>(body[2]).continuation == (count == 1));
            CHECK(std::get<Itemize>(body[3]).items.size() == 1);
        }
    }
}

TEST_CASE("docblock - two blank lines after either table kind start a new paragraph") {
    for (const auto table : {"libtab2", "lib2dtab2"}) {
        for (const int count : {0, 1, 2, 3}) {
            CAPTURE(table, count);
            std::string blanks;
            for (int i = 0; i < count; ++i)
                blanks += "//!\n";
            const auto parsed = parse_docblock(
                std::format("//! \\remarks A table follows:\n//! \\{}[demo.table]{{Caption}}\n"
                            "//! \\column First\n//! \\column Second\n//! \\row Row\n//! \\cell Value\n{}"
                            "//! \\end{}\n{}//! A paragraph follows.\n",
                            table,
                            std::string_view(table) == "lib2dtab2" ? "//! \\cell Other\n" : "",
                            table,
                            blanks));
            REQUIRE(parsed.diags.empty());
            const auto& body = parsed.block.elements.front().body;
            REQUIRE(body.size() == 3);
            CHECK(std::get<BodyParagraph>(body.back()).continuation == (count < 2));
        }
    }
}

TEST_CASE("docblock - blank counts reset between lists and element boundaries") {
    const auto parsed = parse_docblock(
        "//! First:\n//! \\item One.\n//!\n//!\n//! Second:\n//! \\item Two.\n//!\n//! Same paragraph.\n"
        "//! \\remarks Third:\n//! \\item Three.\n//!\n//! Continuation.\n");
    REQUIRE(parsed.diags.empty());
    REQUIRE(parsed.block.leading.size() == 5);
    CHECK_FALSE(std::get<BodyParagraph>(parsed.block.leading[2]).continuation);
    CHECK(std::get<BodyParagraph>(parsed.block.leading[4]).continuation);
    CHECK(std::get<BodyParagraph>(parsed.block.elements.front().body.back()).continuation);
}

TEST_CASE("docblock - doubled block-comment blanks preserve paragraph boundaries") {
    const auto parsed = parse_docblock("/*! Choose one:\n * \\item First.\n *\n *\n * Choose again.\n */");
    REQUIRE(parsed.diags.empty());
    REQUIRE(parsed.block.leading.size() == 3);
    CHECK_FALSE(std::get<BodyParagraph>(parsed.block.leading.back()).continuation);
}

TEST_CASE("docblock - doubled blanks within table cells and code stay literal") {
    const auto parsed = parse_docblock(
        "//! \\remarks A table follows:\n//! \\libtab2[demo.table]{Caption}\n"
        "//! \\column First\n//! \\column Second\n//! \\row Row\n//! \\cell Cell\n//!\n//!\n//! continuation.\n"
        "//! \\endlibtab2\n//! Same paragraph.\n//! \\codeblock\n//! first\n//!\n//!\n//! last\n//! \\endcodeblock\n");
    REQUIRE(parsed.diags.empty());
    const auto& body = parsed.block.elements.front().body;
    REQUIRE(body.size() == 4);
    CHECK(std::get<InlineText>(std::get<Table1D>(body[1]).rows.front().cell2.front()).text == "Cell continuation.");
    CHECK(std::get<BodyParagraph>(body[2]).continuation);
    CHECK(std::get<AuthoredCode>(body[3]).code.text == "first\n\n\nlast");
}

TEST_CASE("docblock - two blank lines before a block open a numbered paragraph with it") {
    const auto opens = [](const BodyBlock& block) {
        return std::visit(
            [](const auto& b) {
                if constexpr (requires { b.opens_paragraph; })
                    return b.opens_paragraph;
                else
                    return false;
            },
            block);
    };
    const std::string_view blocks[] = {
        "//! \\note A note.\n//! \\endnote\n",
        "//! \\example An example.\n//! \\endexample\n",
        "//! \\codeblock\n//! f();\n//! \\endcodeblock\n",
        "//! \\item An entry.\n",
        "//! \\libtab2[demo.table]{Caption}\n//! \\column First\n//! \\column Second\n//! \\row Row\n"
        "//! \\cell Value\n//! \\endlibtab2\n",
    };
    for (const std::string_view prefix : {"", "\\returns "}) {
        for (const auto block : blocks) {
            // A table needs an element tag before it.
            if (prefix.empty() && block.contains("libtab2"))
                continue;
            for (const int count : {0, 1, 2, 3}) {
                CAPTURE(prefix, block, count);
                std::string blanks;
                for (int i = 0; i < count; ++i)
                    blanks += "//!\n";
                const auto parsed = parse_docblock(std::format("//! {}A sentence.\n{}{}", prefix, blanks, block));
                REQUIRE(parsed.diags.empty());
                const auto& body =
                    parsed.block.elements.empty() ? parsed.block.leading : parsed.block.elements.front().body;
                REQUIRE(body.size() == 2);
                CHECK(opens(body[1]) == (count >= 2));
            }
        }
    }
}

TEST_CASE("docblock - a block that cannot open a paragraph of its own is not marked") {
    // The first block of a body opens its paragraph already, and nothing
    // inside a note is numbered.
    const auto parsed = parse_docblock("//! \\returns\n//!\n//!\n//! \\note First.\n//!\n//!\n//! \\codeblock\n"
                                       "//! f();\n//! \\endcodeblock\n//! \\endnote\n");
    REQUIRE(parsed.diags.empty());
    const auto& body = parsed.block.elements.front().body;
    REQUIRE(body.size() == 1);
    const auto& note = std::get<Note>(body.front());
    CHECK_FALSE(note.opens_paragraph);
    REQUIRE(note.body.size() == 2);
    CHECK_FALSE(std::get<AuthoredCode>(note.body[1]).opens_paragraph);
}

TEST_CASE("docblock - a list after two blank lines does not join the list before it") {
    const auto parsed = parse_docblock("//! Choose:\n//! \\item One.\n//!\n//!\n//! \\item Two.\n");
    REQUIRE(parsed.diags.empty());
    REQUIRE(parsed.block.leading.size() == 3);
    CHECK_FALSE(std::get<Itemize>(parsed.block.leading[1]).opens_paragraph);
    CHECK(std::get<Itemize>(parsed.block.leading[2]).opens_paragraph);
}

TEST_CASE("docblock - coverage claims belong to normative constraint elements") {
    const auto valid = parse_docblock("\\constraints\n\\covers !detail::trait<T>\nThe specified condition holds.");
    CHECK(valid.ok());
    REQUIRE(valid.block.elements.size() == 1);
    CHECK(valid.block.elements.front().coverage.size() == 1);
    for (const auto* invalid : {"\\covers trait<T>",
                                "\\remarks\n\\covers trait<T>",
                                "\\constraints\n\\covers",
                                "\\constraints\n\\covers !!trait<T>",
                                "\\constraints\n\\note\n\\covers trait<T>\n\\endnote"}) {
        CAPTURE(invalid);
        CHECK_FALSE(parse_docblock(invalid).ok());
    }
}

TEST_CASE("docblock - coverage metadata does not split authored lists") {
    const auto parsed = parse_docblock(R"(\constraints
\item First condition.
\covers trait<T>
\item Second condition.)");
    REQUIRE(parsed.ok());
    REQUIRE(parsed.block.elements.size() == 1);
    const auto& element = parsed.block.elements.front();
    REQUIRE(element.body.size() == 1);
    REQUIRE(first<Itemize>(element) != nullptr);
    CHECK(items(element).size() == 2);
    CHECK(element.coverage.size() == 1);
}

TEST_CASE("docblock - malformed inline macros report diagnostics and keep their text") {
    const auto unterminated = parse_docblock("//! \\effects Uses \\placeholder{x here.\n");
    CHECK(has_diag(unterminated, Severity::Error, "unterminated \\placeholder macro"));

    const auto empty = parse_docblock("//! \\effects Uses \\oldconcept{} here.\n");
    CHECK(has_diag(empty, Severity::Error, "\\oldconcept requires an argument"));

    const auto cv_arg = parse_docblock("//! \\effects A \\cv{x} qualifier.\n");
    CHECK(has_diag(cv_arg, Severity::Error, "\\cv takes no argument"));

    const auto nested = parse_docblock("//! \\effects A \\placeholder{a{b}c} name.\n");
    REQUIRE(nested.diags.empty());
    REQUIRE(nested.block.elements.size() == 1);
    CHECK(std::get<InlinePlaceholder>(paragraph(nested.block.elements.front())[1]).text == "a{b}c");
}

// Issue #385: a targeted `\omit` names a part of the declaration and keeps it.
TEST_CASE("docblock - omit takes an optional target, which keeps the declaration") {
    const auto noexcept_target = parse_docblock("\\omit noexcept\n\\effects Nothing.");
    REQUIRE(noexcept_target.ok());
    CHECK_FALSE(noexcept_target.block.markers.omit);
    CHECK(noexcept_target.block.markers.omit_target == std::optional<std::string>{"noexcept"});
    const auto default_target = parse_docblock("\\omit default");
    REQUIRE(default_target.ok());
    CHECK(default_target.block.markers.omit_target == std::optional<std::string>{"default"});
    const auto bare = parse_docblock("\\omit");
    REQUIRE(bare.ok());
    CHECK(bare.block.markers.omit);
    CHECK_FALSE(bare.block.markers.omit_target.has_value());
    const auto unknown = parse_docblock("\\omit explicit");
    REQUIRE(unknown.diags.size() == 1);
    CHECK(unknown.diags.front().message == "unknown \\omit target 'explicit'; expected 'noexcept' or 'default'");
}

TEST_CASE("docblock - asdraft is a marker, and covers may name the element it is about") {
    const auto bare = parse_docblock("\\asdraft");
    CHECK(bare.ok());
    CHECK(bare.block.markers.asdraft);
    CHECK(bare.block.elements.empty());

    const auto scoped = parse_docblock("\\asdraft\n\\covers constraints !detail::trait<T>");
    REQUIRE(scoped.ok());
    REQUIRE(scoped.block.kind_covers.size() == 1);
    CHECK(scoped.block.kind_covers.front().kind == ir::ElementKind::Constraints);
    CHECK(scoped.block.elements.empty());

    const auto mandates = parse_docblock("\\asdraft\n\\covers mandates trait<T>");
    REQUIRE(mandates.ok());
    CHECK(mandates.block.kind_covers.front().kind == ir::ElementKind::Mandates);

    // The element-scoped form needs no marker: without one it names a derived
    // element the item's other wording states (issue #386).
    CHECK(parse_docblock("\\covers constraints trait<T>").ok());
    // Authored wording beside the marker keeps the unscoped form working.
    const auto authored = parse_docblock("\\asdraft\n\\constraints\n\\covers !detail::trait<T>\nThe condition holds.");
    REQUIRE(authored.ok());
    CHECK(authored.block.elements.front().coverage.size() == 1);
    CHECK(authored.block.kind_covers.empty());
}

TEST_CASE("docblock - asdraft takes the description, so it excludes omit, merge and elsewhere") {
    for (const auto* marker : {"\\omit", "\\merge", "\\elsewhere"}) {
        CAPTURE(marker);
        CHECK_FALSE(parse_docblock(std::string("\\asdraft\n") + marker).ok());
    }
}

TEST_CASE("docblock - synopsis-here records how many leading blocks come before it") {
    // The marker divides the leading paragraphs (issue #292): its payload is
    // its position, and it ends the paragraph in progress as a blank line does.
    auto r = parse_docblock("//! Opens the subclause.\n"
                            "//!\n"
                            "//! Still before the synopsis.\n"
                            "//! \\synopsis-here\n"
                            "//! After the synopsis.\n"
                            "//! \\remarks And so is this.\n");
    CHECK(r.diags.empty());
    REQUIRE(r.block.markers.synopsis_here.has_value());
    CHECK(*r.block.markers.synopsis_here == 2);
    CHECK(r.block.leading.size() == 3);
    CHECK(r.block.elements.size() == 1);

    // Without it nothing is recorded, and at the very top nothing precedes it.
    CHECK_FALSE(parse_docblock("//! Prose.\n").block.markers.synopsis_here.has_value());
    auto first = parse_docblock("//! \\synopsis-here\n//! Prose.\n");
    REQUIRE(first.block.markers.synopsis_here.has_value());
    CHECK(*first.block.markers.synopsis_here == 0);

    // After a labelled element there is nothing left to divide.
    auto late = parse_docblock("//! \\remarks Something.\n//! \\synopsis-here\n");
    CHECK_FALSE(late.block.markers.synopsis_here.has_value());
    REQUIRE(late.diags.size() == 1);
    CHECK(late.diags.front().severity == beman::specgen::Severity::Error);

    // A second one is reported, and the first stands.
    auto twice = parse_docblock("//! One.\n//! \\synopsis-here\n//! Two.\n//! \\synopsis-here\n");
    REQUIRE(twice.block.markers.synopsis_here.has_value());
    CHECK(*twice.block.markers.synopsis_here == 1);
    REQUIRE(twice.diags.size() == 1);
    CHECK(twice.diags.front().severity == beman::specgen::Severity::Warning);
}

TEST_CASE("docblock - a code block indented under an item is the item's, and the list goes on") {
    // Issue #367: a `\codeblock` indented past the `\item` line before it
    // belongs to that item. One that is not indented ends the list.
    auto r = parse_docblock("//! \\effects\n"
                            "//! \\item If `a`, equivalent to:\n"
                            "//!   \\codeblock\n"
                            "//!   f();\n"
                            "//!   \\endcodeblock\n"
                            "//! \\item Otherwise, no effect.\n"
                            "//! \\codeblock\n"
                            "//! g();\n"
                            "//! \\endcodeblock\n");
    CHECK(r.ok());
    REQUIRE(r.block.elements.size() == 1);
    const auto& body = r.block.elements.front().body;
    REQUIRE(body.size() == 2);
    const auto& list = std::get<Itemize>(body[0]);
    REQUIRE(list.items.size() == 2);
    REQUIRE(list.items[0].code_blocks.size() == 1);
    CHECK(list.items[0].code_blocks[0].text == "  f();");
    CHECK(list.items[1].code_blocks.empty());
    CHECK(std::get<AuthoredCode>(body[1]).code.text == "g();");
}
