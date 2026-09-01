// tests/beman/specgen/docblock.test.cpp                            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/docblock.hpp>
#include <beman/specgen/docblock.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <string>
#include <string_view>
#include <variant>

using namespace beman::specgen::grammar;
namespace ir = beman::specgen::ir;

namespace {

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
    REQUIRE(e.paragraphs.size() == 1);
    const auto& p = e.paragraphs[0];
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
    REQUIRE(e.paragraphs.size() == 2);
    CHECK(std::get<InlineText>(e.paragraphs[0][0]).text == "First paragraph continues here.");
    CHECK(std::get<InlineText>(e.paragraphs[1][0]).text == "Second paragraph.");
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

TEST_CASE("docblock - unknown tag is an error") {
    auto r = parse_docblock("//! \\bogus stuff\n");
    CHECK(!r.ok());
    CHECK(has_diag(r, Severity::Error, "unknown tag \\bogus"));
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

TEST_CASE("docblock - prose before first tag is an error") {
    auto r = parse_docblock("//! Some stray prose.\n//! \\effects Fine.\n");
    CHECK(!r.ok());
    CHECK(has_diag(r, Severity::Error, "prose before first element tag"));
    CHECK(r.block.elements.size() == 1); // element still parsed
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
    REQUIRE(constraints.paragraphs.size() == 1);
    REQUIRE(constraints.items.size() == 2);
    CHECK(std::get<InlineText>(constraints.paragraphs.front().front()).text == "All of the following are true:");
    CHECK(std::get<InlineCode>(constraints.items.at(0).at(0)).code == "A");
    CHECK(std::get<InlineText>(constraints.items.at(0).back()).text == ",");
    CHECK(std::get<InlineCode>(constraints.items.at(1).at(2)).code == "false");
    CHECK(std::get<InlineText>(constraints.items.at(1).back()).text == ".");
}

TEST_CASE("docblock - authored item diagnostics reject unrepresentable shapes") {
    const auto before = parse_docblock("//! \\item orphan\n");
    CHECK(!before.ok());
    CHECK(has_diag(before, Severity::Error, "requires a preceding element tag"));

    const auto empty = parse_docblock("//! \\constraints\n//! \\item\n//! \\effects Fine.\n");
    CHECK(!empty.ok());
    CHECK(has_diag(empty, Severity::Error, "\\item requires content"));

    const auto after = parse_docblock("//! \\constraints\n//! \\item One.\n//!\n//! Stray prose.\n");
    CHECK(!after.ok());
    CHECK(has_diag(after, Severity::Error, "prose after \\item cannot be represented"));

    const auto after_marker =
        parse_docblock("//! \\constraints\n//! \\item One.\n//! \\freestanding\n//! Stray prose.\n");
    CHECK(!after_marker.ok());
    CHECK(has_diag(after_marker, Severity::Error, "prose after \\item cannot be represented"));
}

TEST_CASE("docblock - iref is prose markup outside backticks") {
    auto r = parse_docblock("//! \\remarks \\iref{class.union.general} follows `\\iref{literal}`.\n");
    CHECK(r.ok());
    REQUIRE(r.block.elements.size() == 1);
    const ProseParagraph& para = r.block.elements.front().paragraphs.front();
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

TEST_CASE("docblock - an authored two-dimensional table is terminal element content") {
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
    const auto& table = r.block.elements.front().table;
    REQUIRE(table.has_value());
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
    CHECK(!trailing.ok());
    CHECK(has_diag(trailing, Severity::Error, "prose after \\lib2dtab2"));

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
    CHECK(!duplicate.ok());
    CHECK(has_diag(duplicate, Severity::Error, "only one \\lib2dtab2"));
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
            // The only RestOptional marker has a closed target vocabulary.
            input += " noexcept";
            break;
        }
        input += "\n";

        auto r = parse_docblock(input);
        CHECK(!has_diag(r, Severity::Error, "unknown tag"));
        CHECK(r.ok());

        if (entry.flag != nullptr)
            CHECK(r.block.markers.*(entry.flag));

        if (entry.set_arg != nullptr)
            // The argument text was already stored while parsing `input`
            // above; calling the setter again reports that fact back.
            CHECK(entry.set_arg(r.block.markers, "probe"));

        if (entry.arity == MarkerArity::RestRequired)
            CHECK(!entry.missing_arg_error.empty());
    }
}
