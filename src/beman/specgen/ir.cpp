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

using namespace std::string_view_literals;

// One total table per enumeration: an enumerator missing from it fails to
// compile (foundation/enumeration.hpp), where a hand-sized array or a switch
// with a fallback would answer "?" in its place. The names are part of the
// JSON schema a hand-written validate-mode golden spells out, so the reverse
// lookup reads the same table, and one table cannot drift from itself.
inline constexpr auto kElementNames =
    foundation::table<ElementKind>(foundation::is<ElementKind::Constraints>("constraints"sv),
                                   foundation::is<ElementKind::Mandates>("mandates"sv),
                                   foundation::is<ElementKind::Expects>("expects"sv),
                                   foundation::is<ElementKind::HardExpects>("hardexpects"sv),
                                   foundation::is<ElementKind::Effects>("effects"sv),
                                   foundation::is<ElementKind::Sync>("sync"sv),
                                   foundation::is<ElementKind::Ensures>("ensures"sv),
                                   foundation::is<ElementKind::Result>("result"sv),
                                   foundation::is<ElementKind::Returns>("returns"sv),
                                   foundation::is<ElementKind::Throws>("throws"sv),
                                   foundation::is<ElementKind::Complexity>("complexity"sv),
                                   foundation::is<ElementKind::Remarks>("remarks"sv),
                                   foundation::is<ElementKind::Errors>("errors"sv));

inline constexpr auto kDispositionNames =
    foundation::table<Disposition>(foundation::is<Disposition::Described>("described"sv),
                                   foundation::is<Disposition::Routed>("routed"sv),
                                   foundation::is<Disposition::Merged>("merged"sv),
                                   foundation::is<Disposition::Omitted>("omitted"sv),
                                   foundation::is<Disposition::Defaulted>("defaulted"sv),
                                   foundation::is<Disposition::Declared>("declared"sv),
                                   foundation::is<Disposition::Expos>("expos"sv),
                                   foundation::is<Disposition::Private>("private"sv),
                                   foundation::is<Disposition::Undocumented>("undocumented"sv));

inline constexpr auto kMemberKindNames =
    foundation::table<MemberKind>(foundation::is<MemberKind::Function>("function"sv),
                                  foundation::is<MemberKind::Data>("data"sv),
                                  foundation::is<MemberKind::Alias>("alias"sv));

inline constexpr auto kSpanKindNames =
    foundation::table<SpanKind>(foundation::is<SpanKind::ExposId>("exposid"sv),
                                foundation::is<SpanKind::SeeBelow>("seebelow"sv),
                                foundation::is<SpanKind::ImplDefined>("impl-defined"sv),
                                foundation::is<SpanKind::Placeholder>("placeholder"sv),
                                foundation::is<SpanKind::Ref>("ref"sv),
                                foundation::is<SpanKind::LibraryIndex>("library-index"sv));

inline constexpr auto kIndexKindNames =
    foundation::table<IndexKind>(foundation::is<IndexKind::Global>("global"sv),
                                 foundation::is<IndexKind::Constructor>("ctor"sv),
                                 foundation::is<IndexKind::Destructor>("dtor"sv),
                                 foundation::is<IndexKind::Member>("member"sv),
                                 foundation::is<IndexKind::MemberX>("memberx"sv),
                                 foundation::is<IndexKind::MemberExpos>("memberexpos"sv),
                                 foundation::is<IndexKind::Zombie>("zombie"sv),
                                 foundation::is<IndexKind::Misc>("misc"sv));

template <class E>
std::optional<E> enumerator_named(const foundation::total_table<E, std::string_view>& names, std::string_view name) {
    const auto found = std::ranges::find_if(foundation::enumerators<E>, [&](E e) { return names[e] == name; });
    return found != foundation::enumerators<E>.end() ? std::optional<E>{*found} : std::nullopt;
}

std::string_view        span_kind_name(SpanKind k) { return kSpanKindNames[k]; }
std::optional<SpanKind> span_kind_from_name(std::string_view s) { return enumerator_named(kSpanKindNames, s); }

std::string_view         index_kind_name(IndexKind k) { return kIndexKindNames[k]; }
std::optional<IndexKind> index_kind_from_name(std::string_view s) { return enumerator_named(kIndexKindNames, s); }

} // namespace

std::string_view           element_name(ElementKind k) { return kElementNames[k]; }
std::optional<ElementKind> element_from_name(std::string_view s) { return enumerator_named(kElementNames, s); }

std::string_view           disposition_name(Disposition d) { return kDispositionNames[d]; }
std::optional<Disposition> disposition_from_name(std::string_view s) { return enumerator_named(kDispositionNames, s); }

std::string_view          member_kind_name(MemberKind k) { return kMemberKindNames[k]; }
std::optional<MemberKind> member_kind_from_name(std::string_view s) { return enumerator_named(kMemberKindNames, s); }

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
