// src/beman/specgen/ir_json.cpp                                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/ir_json.hpp>

#include <beman/specgen/ir.hpp>
#include <beman/specgen/json/descriptor.hpp>
#include <beman/specgen/json/reader.hpp>

#include <expected>
#include <string>
#include <string_view>
#include <tuple>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>
#include <variant>

namespace beman::specgen::ir {

// --- the schema: one member-descriptor table per IR type (decision json-single-schema) ---
//
// Each specialization below is the *entire* JSON shape of its type: field
// names, order, and how each is read/written. json/descriptor.hpp
// walks these same tables for both emit_json (json::emit_value /
// json::emit_variant) and parse (json::parse_value), so there is
// exactly one place that knows, say, that a Span is {begin, end, kind,
// payload} in that order -- adding, renaming, or reordering a field is a
// one-line edit here, not a synchronized edit to a hand-written emitter and
// a hand-written Reader::object() callback.
//
// A tagged variant's "type"/"t" key is not one of its alternatives' own
// fields: Node's and Inline's alts tables supply it, keyed by alternative
// type, so Section/Synopsis/SpecItem/FreeParagraph and
// TextInline/CodeInline/RefInline/ConceptRef describe only their own data.

} // namespace beman::specgen::ir

// json_descriptor is a json template, so its specializations are
// declared here rather than in namespace ir; each one names its ir:: type
// explicitly.
namespace beman::specgen::json {

using beman::specgen::ir::BodyUse;
using beman::specgen::ir::Change;
using beman::specgen::ir::change_from_name;
using beman::specgen::ir::change_name;
using beman::specgen::ir::ChangedInline;
using beman::specgen::ir::ChangeRun;
using beman::specgen::ir::CodeInline;
using beman::specgen::ir::CodeText;
using beman::specgen::ir::ConceptRef;
using beman::specgen::ir::DescriptionElement;
using beman::specgen::ir::Disposition;
using beman::specgen::ir::disposition_from_name;
using beman::specgen::ir::disposition_name;
using beman::specgen::ir::Document;
using beman::specgen::ir::element_from_name;
using beman::specgen::ir::element_name;
using beman::specgen::ir::EquivalentTo;
using beman::specgen::ir::ForeignDeclaration;
using beman::specgen::ir::ForeignNamespace;
using beman::specgen::ir::FreeParagraph;
using beman::specgen::ir::index_kind_from_name;
using beman::specgen::ir::index_kind_name;
using beman::specgen::ir::IndexEntry;
using beman::specgen::ir::Inline;
using beman::specgen::ir::ItemDecl;
using beman::specgen::ir::ItemDescr;
using beman::specgen::ir::Itemize;
using beman::specgen::ir::member_kind_from_name;
using beman::specgen::ir::member_kind_name;
using beman::specgen::ir::MemberKind;
using beman::specgen::ir::Node;
using beman::specgen::ir::PlainInline;
using beman::specgen::ir::RefInline;
using beman::specgen::ir::Section;
using beman::specgen::ir::Span;
using beman::specgen::ir::span_kind_from_name;
using beman::specgen::ir::span_kind_name;
using beman::specgen::ir::SpecItem;
using beman::specgen::ir::Synopsis;
using beman::specgen::ir::SynopsisEntry;
using beman::specgen::ir::Table1D;
using beman::specgen::ir::Table1DRow;
using beman::specgen::ir::Table2D;
using beman::specgen::ir::Table2DRow;
using beman::specgen::ir::TextInline;

template <>
struct json_descriptor<Span> {
    static constexpr auto members = std::tuple{
        field("begin", &Span::begin),
        field("end", &Span::end),
        enum_field("kind", &Span::kind, span_kind_name, span_kind_from_name, "span kind"),
        field("payload", &Span::payload),
    };
};

template <>
struct json_descriptor<ChangeRun> {
    static constexpr auto members = std::tuple{
        field("begin", &ChangeRun::begin),
        field("end", &ChangeRun::end),
        enum_field("change", &ChangeRun::change, change_name, change_from_name, "change"),
    };
};

// `changes` is omitted when empty (docs/plans/wording-diff.md): the marks
// are the one field added to this frozen schema after the fact, and an
// unmarked document has to serialize exactly as it did before they existed.
// The same holds for every `change` key below.
template <>
struct json_descriptor<CodeText> {
    static constexpr auto members = std::tuple{field("text", &CodeText::text),
                                               field("spans", &CodeText::spans),
                                               defaulted_field("changes", &CodeText::changes)};
};

template <>
struct json_descriptor<IndexEntry> {
    static constexpr auto members = std::tuple{
        enum_field("kind", &IndexEntry::kind, index_kind_name, index_kind_from_name, "index kind"),
        field("name", &IndexEntry::name),
        field("parent", &IndexEntry::parent),
    };
};

template <>
struct json_descriptor<ItemDecl> {
    static constexpr auto members = std::tuple{field("signatures", &ItemDecl::signatures),
                                               field("index", &ItemDecl::index),
                                               field("entities", &ItemDecl::entities)};
};

template <>
struct json_descriptor<Table2DRow> {
    static constexpr auto members = std::tuple{
        field("header", &Table2DRow::header),
        field("cell1", &Table2DRow::cell1),
        field("cell2", &Table2DRow::cell2),
    };
};

template <>
struct json_descriptor<Table2D> {
    static constexpr auto members = std::tuple{
        field("stable", &Table2D::stable_name),
        field("caption", &Table2D::caption),
        field("column1", &Table2D::column1),
        field("column2", &Table2D::column2),
        field("rows", &Table2D::rows),
        defaulted_enum_field("change", &Table2D::change, Change::Same, change_name, change_from_name, "change"),
    };
};

template <>
struct json_descriptor<Table1DRow> {
    static constexpr auto members = std::tuple{
        field("cell1", &Table1DRow::cell1),
        field("cell2", &Table1DRow::cell2),
    };
};

template <>
struct json_descriptor<Table1D> {
    static constexpr auto members = std::tuple{
        field("stable", &Table1D::stable_name),
        field("caption", &Table1D::caption),
        field("column1", &Table1D::column1),
        field("column2", &Table1D::column2),
        field("rows", &Table1D::rows),
        defaulted_enum_field("change", &Table1D::change, Change::Same, change_name, change_from_name, "change"),
    };
};

// Every block kind's `change` is omitted at `Same`, like a node's.
template <>
struct json_descriptor<ir::BodyParagraph> {
    static constexpr auto members = std::tuple{
        field("text", &ir::BodyParagraph::text),
        field("continuation", &ir::BodyParagraph::continuation),
        defaulted_enum_field(
            "change", &ir::BodyParagraph::change, Change::Same, change_name, change_from_name, "change"),
    };
};

template <>
struct json_descriptor<Itemize> {
    static constexpr auto members = std::tuple{
        field("items", &Itemize::items),
        defaulted_enum_field("change", &Itemize::change, Change::Same, change_name, change_from_name, "change"),
    };
};

template <>
struct json_descriptor<EquivalentTo> {
    static constexpr auto members = std::tuple{
        field("code", &EquivalentTo::code),
        defaulted_enum_field("change", &EquivalentTo::change, Change::Same, change_name, change_from_name, "change"),
    };
};

template <>
struct json_descriptor<ir::AuthoredCode> {
    static constexpr auto members = std::tuple{
        field("code", &ir::AuthoredCode::code),
        defaulted_enum_field(
            "change", &ir::AuthoredCode::change, Change::Same, change_name, change_from_name, "change"),
    };
};

template <>
struct json_descriptor<ir::InformativeBlock> {
    static constexpr auto alts = alternatives("block",
                                              "informative block",
                                              alt<ir::BodyParagraph>("paragraph"),
                                              alt<Itemize>("itemize"),
                                              alt<Table2D>("table"),
                                              alt<Table1D>("flat-table"),
                                              alt<ir::AuthoredCode>("code"));
};

template <>
struct json_descriptor<ir::Note> {
    static constexpr auto members = std::tuple{
        field("body", &ir::Note::body),
        defaulted_enum_field("change", &ir::Note::change, Change::Same, change_name, change_from_name, "change"),
    };
};

template <>
struct json_descriptor<ir::Example> {
    static constexpr auto members = std::tuple{
        field("body", &ir::Example::body),
        defaulted_enum_field("change", &ir::Example::change, Change::Same, change_name, change_from_name, "change"),
    };
};

template <>
struct json_descriptor<ir::BodyBlock> {
    static constexpr auto alts = alternatives("block",
                                              "description block",
                                              alt<ir::BodyParagraph>("paragraph"),
                                              alt<Itemize>("itemize"),
                                              alt<Table2D>("table"),
                                              alt<Table1D>("flat-table"),
                                              alt<EquivalentTo>("equivalent"),
                                              alt<ir::AuthoredCode>("code"),
                                              alt<ir::Note>("note"),
                                              alt<ir::Example>("example"));
};

// Temporary migration carrier, never a second representation in semantic IR.
// Optional legacy fields are read but never emitted by to_representation.
struct ElementWire {
    ir::ElementKind                           kind = ir::ElementKind::Effects;
    std::optional<ir::DescriptionBody>        body;
    std::optional<std::vector<ir::Paragraph>> paragraphs;
    std::optional<Itemize>                    itemize;
    std::optional<Table2D>                    table;
    std::optional<Table1D>                    flat_table;
    std::optional<EquivalentTo>               equivalent;
    bool                                      derived = false;
    std::vector<ir::Paragraph>                conjuncts;
    std::vector<ir::Paragraph>                coverage;
    ir::Change                                change = ir::Change::Same; // the wording diff's mark
};

template <>
struct json_descriptor<ElementWire> {
    static constexpr auto members = std::tuple{
        enum_field("kind", &ElementWire::kind, element_name, element_from_name, "element kind"),
        optional_field("body", &ElementWire::body),
        optional_field("paragraphs", &ElementWire::paragraphs),
        optional_projected_field("itemize", &ElementWire::itemize, &Itemize::items),
        optional_field("table", &ElementWire::table),
        optional_field("flat_table", &ElementWire::flat_table),
        optional_projected_field("equivalent", &ElementWire::equivalent, &EquivalentTo::code),
        field("derived", &ElementWire::derived),
        field("conjuncts", &ElementWire::conjuncts),
        field("coverage", &ElementWire::coverage),
        defaulted_enum_field("change", &ElementWire::change, Change::Same, change_name, change_from_name, "change"),
    };
};

template <>
struct json_descriptor<DescriptionElement> {
    using representation = ElementWire;
    static representation to_representation(const DescriptionElement&);
    static bool           from_representation(Reader&, representation, DescriptionElement&);
};

ElementWire json_descriptor<DescriptionElement>::to_representation(const DescriptionElement& e) {
    ElementWire w;
    w.kind      = e.kind;
    w.body      = e.body;
    w.derived   = e.derived;
    w.conjuncts = e.conjuncts;
    w.coverage  = e.coverage;
    w.change    = e.change;
    return w;
}

bool json_descriptor<DescriptionElement>::from_representation(Reader& r, ElementWire w, DescriptionElement& e) {
    if (w.body && (w.paragraphs || w.itemize || w.table || w.flat_table || w.equivalent))
        return r.fail("description mixes ordered body and legacy fields");
    e           = DescriptionElement{};
    e.kind      = w.kind;
    e.derived   = w.derived;
    e.conjuncts = std::move(w.conjuncts);
    e.coverage  = std::move(w.coverage);
    e.change    = w.change;
    if (w.body) {
        e.body = std::move(*w.body);
    } else {
        if (w.paragraphs)
            e.body = std::move(*w.paragraphs) | std::views::as_rvalue |
                     std::views::transform(
                         [](ir::Paragraph p) -> ir::BodyBlock { return ir::BodyParagraph{std::move(p)}; }) |
                     std::ranges::to<ir::DescriptionBody>();
        if (w.itemize)
            e.body.push_back(std::move(*w.itemize));
        if (w.table)
            e.body.push_back(std::move(*w.table));
        if (w.flat_table)
            e.body.push_back(std::move(*w.flat_table));
        if (w.equivalent)
            e.body.push_back(std::move(*w.equivalent));
    }
    return true;
}

template <>
struct json_descriptor<ItemDescr> {
    static constexpr auto members =
        std::tuple{field("elements", &ItemDescr::elements), field("leading", &ItemDescr::leading)};
};

template <>
struct json_descriptor<TextInline> {
    static constexpr auto members = std::tuple{field("text", &TextInline::text)};
};

template <>
struct json_descriptor<CodeInline> {
    static constexpr auto members = std::tuple{field("code", &CodeInline::code)};
};

template <>
struct json_descriptor<RefInline> {
    static constexpr auto members = std::tuple{field("stable", &RefInline::stable_name)};
};

template <>
struct json_descriptor<ConceptRef> {
    static constexpr auto members = std::tuple{field("name", &ConceptRef::name)};
};

// A plain inline inside a changed run serializes exactly as the same inline
// does outside one: the two variants share the tag key and the four tags.
template <>
struct json_descriptor<PlainInline> {
    static constexpr auto alts = alternatives("t",
                                              "inline type",
                                              alt<TextInline>("text"),
                                              alt<CodeInline>("code"),
                                              alt<RefInline>("ref"),
                                              alt<ConceptRef>("concept"));
};

template <>
struct json_descriptor<ChangedInline> {
    static constexpr auto members = std::tuple{
        enum_field("change", &ChangedInline::change, change_name, change_from_name, "change"),
        field("inlines", &ChangedInline::inlines),
    };
};

template <>
struct json_descriptor<Inline> {
    static constexpr auto alts = alternatives("t",
                                              "inline type",
                                              alt<TextInline>("text"),
                                              alt<CodeInline>("code"),
                                              alt<RefInline>("ref"),
                                              alt<ConceptRef>("concept"),
                                              alt<ChangedInline>("changed"));
};

template <>
struct json_descriptor<SpecItem> {
    static constexpr auto members = std::tuple{
        field("decl", &SpecItem::decl),
        field("descr", &SpecItem::descr),
        defaulted_enum_field("change", &SpecItem::change, Change::Same, change_name, change_from_name, "change"),
    };
};

template <>
struct json_descriptor<Section> {
    static constexpr auto members = std::tuple{
        field("stable", &Section::stable_name),
        field("title", &Section::title),
        field("children", &Section::children),
        defaulted_enum_field("change", &Section::change, Change::Same, change_name, change_from_name, "change"),
    };
};

template <>
struct json_descriptor<SynopsisEntry> {
    static constexpr auto members = std::tuple{
        field("name", &SynopsisEntry::name),
        enum_field("disposition", &SynopsisEntry::disposition, disposition_name, disposition_from_name, "disposition"),
        field("section", &SynopsisEntry::section),
        enum_field("kind", &SynopsisEntry::kind, member_kind_name, member_kind_from_name, "member kind"),
        field("parent", &SynopsisEntry::parent),
    };
};

template <>
struct json_descriptor<Synopsis> {
    static constexpr auto members = std::tuple{
        field("name", &Synopsis::name),
        field("code", &Synopsis::code),
        field("roster", &Synopsis::roster),
        defaulted_enum_field("change", &Synopsis::change, Change::Same, change_name, change_from_name, "change"),
    };
};

template <>
struct json_descriptor<FreeParagraph> {
    static constexpr auto members = std::tuple{
        field("content", &FreeParagraph::text),
        defaulted_enum_field("change", &FreeParagraph::change, Change::Same, change_name, change_from_name, "change"),
    };
};

template <>
struct json_descriptor<Node> {
    static constexpr std::string_view item_aliases[] = {"item", "item-v2", "item-v3"};
    static constexpr auto             alts           = alternatives("type",
                                                                    "node type",
                                                                    alt<Section>("section"),
                                                                    alt<Synopsis>("synopsis"),
                                                                    alt<SpecItem>("item-v4", item_aliases),
                                                                    alt<FreeParagraph>("para"));
};

template <>
struct json_descriptor<ForeignNamespace> {
    static constexpr auto members = std::tuple{
        field("name", &ForeignNamespace::name),
        field("qualified", &ForeignNamespace::qualified),
    };
};

template <>
struct json_descriptor<ForeignDeclaration> {
    static constexpr auto members = std::tuple{
        field("name", &ForeignDeclaration::name),
        field("header", &ForeignDeclaration::header),
    };
};

template <>
struct json_descriptor<BodyUse> {
    static constexpr auto members = std::tuple{
        field("function", &BodyUse::function),
        field("member", &BodyUse::member),
    };
};

template <>
struct json_descriptor<Document> {
    static constexpr auto members = std::tuple{
        field("nodes", &Document::nodes),
        optional_field("paper_entities", &Document::paper_entities),
        field("foreign", &Document::foreign_namespaces),
        field("foreign_decls", &Document::foreign_declarations),
        field("body_uses", &Document::unextracted_uses),
    };
};

} // namespace beman::specgen::json

namespace beman::specgen::ir {

// --- public entry points: thin wrappers over the generic engine ------------

std::string emit_json(const CodeText& c) {
    std::string out;
    json::emit_json_described(c, out, json::json_descriptor<CodeText>::members);
    return out;
}

std::string emit_json(const Node& node) {
    std::string out;
    json::emit_variant(node, out);
    return out;
}

std::string emit_json(const SpecItem& item) { return emit_json(Node{item}); }

std::string emit_json(const Document& doc) {
    std::string out;
    json::emit_json_described(doc, out, json::json_descriptor<Document>::members);
    return out;
}

namespace {

// Shared tail: require the whole input to be consumed, and report errors.
// The engine underneath (json::Reader/parse_value, frozen —
// see this file's schema note above) is bool-returning; this is the
// one seam that lifts that into the expected carrier (decision
// expected-error-taxonomy: a single fallible step -> std::expected<T, E>),
// so callers get `.and_then(...)` instead of
// an `if (!r.has_value()) return r;` ladder.
template <class T, class Read>
std::expected<T, ParseError> finish(std::string_view text, Read read) {
    json::Reader r(text);
    T            value{};
    if (!read(r, value))
        return std::unexpected(ParseError{r.offset(), r.message()});
    if (!r.at_end()) {
        r.fail("trailing content after value");
        return std::unexpected(ParseError{r.offset(), r.message()});
    }
    return value;
}

} // namespace

std::expected<Document, ParseError> parse_document(std::string_view text) {
    return finish<Document>(text, [](json::Reader& r, Document& out) { return json::parse_value(r, out); });
}

std::expected<SpecItem, ParseError> parse_item(std::string_view text) {
    return finish<SpecItem>(text, [](json::Reader& r, SpecItem& out) {
        Node node;
        if (!json::parse_value(r, node))
            return false;
        if (const auto* item = std::get_if<SpecItem>(&node)) {
            out = *item;
            return true;
        }
        return r.fail("expected an item node");
    });
}

std::expected<CodeText, ParseError> parse_code(std::string_view text) {
    return finish<CodeText>(text, [](json::Reader& r, CodeText& out) { return json::parse_value(r, out); });
}

} // namespace beman::specgen::ir
