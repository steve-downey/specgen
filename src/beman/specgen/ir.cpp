// src/beman/specgen/ir.cpp                                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/ir.hpp>

#include <beman/specgen/foundation/json_descriptor.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>

namespace beman::specgen::ir {

namespace {

constexpr std::array<std::pair<ElementKind, std::string_view>, 13> kElementNames{{
    {ElementKind::Constraints, "constraints"},
    {ElementKind::Mandates, "mandates"},
    {ElementKind::Expects, "expects"},
    {ElementKind::HardExpects, "hardexpects"},
    {ElementKind::Effects, "effects"},
    {ElementKind::Sync, "sync"},
    {ElementKind::Ensures, "ensures"},
    {ElementKind::Result, "result"},
    {ElementKind::Returns, "returns"},
    {ElementKind::Throws, "throws"},
    {ElementKind::Complexity, "complexity"},
    {ElementKind::Remarks, "remarks"},
    {ElementKind::Errors, "errors"},
}};

// Table-driven like kElementNames rather than a switch pair, because
// this enum's names are part of the JSON schema a hand-written validate-mode
// golden spells out, and one table cannot drift the way two switches can.
constexpr std::array<std::pair<Disposition, std::string_view>, 8> kDispositionNames{{
    {Disposition::Described, "described"},
    {Disposition::Routed, "routed"},
    {Disposition::Merged, "merged"},
    {Disposition::Omitted, "omitted"},
    {Disposition::Defaulted, "defaulted"},
    {Disposition::Expos, "expos"},
    {Disposition::Private, "private"},
    {Disposition::Undocumented, "undocumented"},
}};

constexpr std::array<std::pair<MemberKind, std::string_view>, 3> kMemberKindNames{{
    {MemberKind::Function, "function"},
    {MemberKind::Data, "data"},
    {MemberKind::Alias, "alias"},
}};

std::string_view span_kind_name(SpanKind k) {
    switch (k) {
    case SpanKind::ExposId:
        return "exposid";
    case SpanKind::SeeBelow:
        return "seebelow";
    case SpanKind::ImplDefined:
        return "impl-defined";
    case SpanKind::Placeholder:
        return "placeholder";
    case SpanKind::Ref:
        return "ref";
    case SpanKind::LibraryIndex:
        return "library-index";
    }
    return "?";
}

std::optional<SpanKind> span_kind_from_name(std::string_view s) {
    if (s == "exposid")
        return SpanKind::ExposId;
    if (s == "seebelow")
        return SpanKind::SeeBelow;
    if (s == "impl-defined")
        return SpanKind::ImplDefined;
    if (s == "placeholder")
        return SpanKind::Placeholder;
    if (s == "ref")
        return SpanKind::Ref;
    if (s == "library-index")
        return SpanKind::LibraryIndex;
    return std::nullopt;
}

std::string_view index_kind_name(IndexKind k) {
    switch (k) {
    case IndexKind::Global:
        return "global";
    case IndexKind::Constructor:
        return "ctor";
    case IndexKind::Destructor:
        return "dtor";
    case IndexKind::Member:
        return "member";
    case IndexKind::MemberX:
        return "memberx";
    case IndexKind::MemberExpos:
        return "memberexpos";
    case IndexKind::Zombie:
        return "zombie";
    case IndexKind::Misc:
        return "misc";
    }
    return "?";
}

std::optional<IndexKind> index_kind_from_name(std::string_view s) {
    if (s == "global")
        return IndexKind::Global;
    if (s == "ctor")
        return IndexKind::Constructor;
    if (s == "dtor")
        return IndexKind::Destructor;
    if (s == "member")
        return IndexKind::Member;
    if (s == "memberx")
        return IndexKind::MemberX;
    if (s == "memberexpos")
        return IndexKind::MemberExpos;
    if (s == "zombie")
        return IndexKind::Zombie;
    if (s == "misc")
        return IndexKind::Misc;
    return std::nullopt;
}

} // namespace

std::string_view element_name(ElementKind k) {
    auto it = std::ranges::find_if(kElementNames, [k](const auto& entry) { return entry.first == k; });
    return it != kElementNames.end() ? it->second : "?";
}

std::optional<ElementKind> element_from_name(std::string_view s) {
    auto it = std::ranges::find_if(kElementNames, [s](const auto& entry) { return entry.second == s; });
    if (it != kElementNames.end())
        return it->first;
    return std::nullopt;
}

std::string_view disposition_name(Disposition d) {
    auto it = std::ranges::find_if(kDispositionNames, [d](const auto& entry) { return entry.first == d; });
    return it != kDispositionNames.end() ? it->second : "?";
}

std::optional<Disposition> disposition_from_name(std::string_view s) {
    auto it = std::ranges::find_if(kDispositionNames, [s](const auto& entry) { return entry.second == s; });
    if (it != kDispositionNames.end())
        return it->first;
    return std::nullopt;
}

std::string_view member_kind_name(MemberKind k) {
    auto it = std::ranges::find_if(kMemberKindNames, [k](const auto& entry) { return entry.first == k; });
    return it != kMemberKindNames.end() ? it->second : "?";
}

std::optional<MemberKind> member_kind_from_name(std::string_view s) {
    auto it = std::ranges::find_if(kMemberKindNames, [s](const auto& entry) { return entry.second == s; });
    if (it != kMemberKindNames.end())
        return it->first;
    return std::nullopt;
}

void canonicalize(ItemDescr& d) {
    // Within one kind (design §5.2), a derived element sorts ahead of
    // an authored one ("derived conjuncts first, authored prose appended"),
    // so the secondary key is `!derived` -- `derived` sorts as 0, ahead of
    // an authored element's 1. Stable, so two elements that tie on both keys
    // (both derived, or both authored -- not produced today, but not ruled
    // out) keep their relative order.
    std::stable_sort(
        d.elements.begin(), d.elements.end(), [](const DescriptionElement& a, const DescriptionElement& b) {
            return std::pair(canonical_rank(a.kind), !a.derived) < std::pair(canonical_rank(b.kind), !b.derived);
        });
}

// --- the schema: one member-descriptor table per IR type (decision json-single-schema) ---
//
// Each specialization below is the *entire* JSON shape of its type: field
// names, order, and how each is read/written. foundation/json_descriptor.hpp
// walks these same tables for both emit_json (foundation::emit_value /
// foundation::emit_variant) and parse (foundation::parse_value), so there is
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

// json_descriptor is a foundation::-namespace template, so its
// specializations are declared here rather than in namespace ir; each one
// names its ir:: type explicitly.
namespace beman::specgen::foundation {

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
    static constexpr auto members =
        std::tuple{field("signatures", &ItemDecl::signatures), field("index", &ItemDecl::index)};
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
struct json_descriptor<DescriptionElement> {
    static constexpr auto members = std::tuple{
        enum_field("kind", &DescriptionElement::kind, element_name, element_from_name, "element kind"),
        field("paragraphs", &DescriptionElement::paragraphs),
        optional_projected_field("itemize", &DescriptionElement::itemize, &Itemize::items),
        optional_field("table", &DescriptionElement::table),
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
        field("foreign", &Document::foreign_namespaces),
        field("body_uses", &Document::unextracted_uses),
    };
};

} // namespace beman::specgen::foundation

namespace beman::specgen::ir {

// --- public entry points: thin wrappers over the generic engine ------------

std::string emit_json(const CodeText& c) {
    std::string out;
    foundation::emit_json_described(c, out, foundation::json_descriptor<CodeText>::members);
    return out;
}

std::string emit_json(const Node& node) {
    std::string out;
    foundation::emit_variant(node, out);
    return out;
}

std::string emit_json(const SpecItem& item) { return emit_json(Node{item}); }

std::string emit_json(const Document& doc) {
    std::string out;
    foundation::emit_json_described(doc, out, foundation::json_descriptor<Document>::members);
    return out;
}

namespace {

// Shared tail: require the whole input to be consumed, and report errors.
// The engine underneath (foundation::Reader/parse_value, frozen —
// see this file's schema note above) is bool-returning; this is the
// one seam that lifts that into the expected carrier (decision
// expected-error-taxonomy: a single fallible step -> std::expected<T, E>),
// so callers get `.and_then(...)` instead of
// an `if (!r.has_value()) return r;` ladder.
template <class T, class Read>
std::expected<T, ParseError> finish(std::string_view text, Read read) {
    foundation::Reader r(text);
    T                  value{};
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
    return finish<Document>(text,
                            [](foundation::Reader& r, Document& out) { return foundation::parse_value(r, out); });
}

std::expected<SpecItem, ParseError> parse_item(std::string_view text) {
    return finish<SpecItem>(text, [](foundation::Reader& r, SpecItem& out) {
        Node node;
        if (!foundation::parse_value(r, node))
            return false;
        if (const auto* item = std::get_if<SpecItem>(&node)) {
            out = *item;
            return true;
        }
        return r.fail("expected an item node");
    });
}

std::expected<CodeText, ParseError> parse_code(std::string_view text) {
    return finish<CodeText>(text,
                            [](foundation::Reader& r, CodeText& out) { return foundation::parse_value(r, out); });
}

// --- transitional shims -----------------------------------------------------
// Thin optional/ParseError* wrappers over the expected-returning entry points
// above, kept so existing callers migrate on their own
// schedule instead of all moving at once.

std::optional<Document> parse_json_document(std::string_view text, ParseError* error) {
    auto result = parse_document(text);
    if (result)
        return *std::move(result);
    if (error != nullptr)
        *error = result.error();
    return std::nullopt;
}

std::optional<SpecItem> parse_json_item(std::string_view text, ParseError* error) {
    auto result = parse_item(text);
    if (result)
        return *std::move(result);
    if (error != nullptr)
        *error = result.error();
    return std::nullopt;
}

std::optional<CodeText> parse_json_code(std::string_view text, ParseError* error) {
    auto result = parse_code(text);
    if (result)
        return *std::move(result);
    if (error != nullptr)
        *error = result.error();
    return std::nullopt;
}

} // namespace beman::specgen::ir
