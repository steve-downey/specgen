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
struct json_descriptor<CodeText> {
    static constexpr auto members = std::tuple{field("text", &CodeText::text), field("spans", &CodeText::spans)};
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
    };
};

template <>
struct json_descriptor<DescriptionElement> {
    static constexpr auto members = std::tuple{
        enum_field("kind", &DescriptionElement::kind, element_name, element_from_name, "element kind"),
        field("paragraphs", &DescriptionElement::paragraphs),
        optional_projected_field("itemize", &DescriptionElement::itemize, &Itemize::items),
        optional_field("table", &DescriptionElement::table),
        optional_field("flat_table", &DescriptionElement::flat_table),
        optional_projected_field("equivalent", &DescriptionElement::equivalent, &EquivalentTo::code),
        field("derived", &DescriptionElement::derived),
        field("conjuncts", &DescriptionElement::conjuncts),
    };
};

template <>
struct json_descriptor<ItemDescr> {
    static constexpr auto members = std::tuple{field("elements", &ItemDescr::elements)};
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

template <>
struct json_descriptor<Inline> {
    static constexpr auto alts = alternatives("t",
                                              "inline type",
                                              alt<TextInline>("text"),
                                              alt<CodeInline>("code"),
                                              alt<RefInline>("ref"),
                                              alt<ConceptRef>("concept"));
};

template <>
struct json_descriptor<SpecItem> {
    static constexpr auto members = std::tuple{field("decl", &SpecItem::decl), field("descr", &SpecItem::descr)};
};

template <>
struct json_descriptor<Section> {
    static constexpr auto members = std::tuple{
        field("stable", &Section::stable_name),
        field("title", &Section::title),
        field("children", &Section::children),
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
    };
};

template <>
struct json_descriptor<FreeParagraph> {
    static constexpr auto members = std::tuple{field("content", &FreeParagraph::text)};
};

template <>
struct json_descriptor<Node> {
    static constexpr auto alts = alternatives("type",
                                              "node type",
                                              alt<Section>("section"),
                                              alt<Synopsis>("synopsis"),
                                              alt<SpecItem>("item"),
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
