// src/beman/specgen/ir.cpp                                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/ir.hpp>
#include <beman/specgen/foundation/overloaded.hpp>
#include <ranges>
#include <variant>
#include <vector>

#include <beman/specgen/foundation/enumeration.hpp>

#include <algorithm>
#include <cstddef>
#include <array>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <beman/specgen/foundation/overloaded.hpp>

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
                                   foundation::is<ElementKind::Errors>("errors"sv),
                                   foundation::is<ElementKind::Recommended>("recommended"sv));

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
                                foundation::is<SpanKind::LibraryIndex>("library-index"sv),
                                foundation::is<SpanKind::CvQualifier>("cvqual"sv),
                                foundation::is<SpanKind::Optional>("opt"sv),
                                foundation::is<SpanKind::MathVariable>("math-variable"sv),
                                foundation::is<SpanKind::Subscript>("subscript"sv),
                                foundation::is<SpanKind::CodeSubscript>("code-subscript"sv),
                                foundation::is<SpanKind::Math>("math"sv),
                                foundation::is<SpanKind::Ordinal>("ordinal"sv),
                                foundation::is<SpanKind::Ellipsis>("ellipsis"sv));

inline constexpr auto kLayoutHintNames =
    foundation::table<LayoutHintKind>(foundation::is<LayoutHintKind::Break>("brk"sv),
                                      foundation::is<LayoutHintKind::Hyphen>("hyphen"sv),
                                      foundation::is<LayoutHintKind::LineBreak>("linebreak"sv),
                                      foundation::is<LayoutHintKind::NewLine>("newline"sv),
                                      foundation::is<LayoutHintKind::Tie>("tie"sv),
                                      foundation::is<LayoutHintKind::MathOpen>("math-open"sv));

inline constexpr auto kTermRoleNames =
    foundation::table<TermRole>(foundation::is<TermRole::Term>("term"sv),
                                foundation::is<TermRole::Definition>("definition"sv),
                                foundation::is<TermRole::ElementName>("element-name"sv));

inline constexpr auto kPhraseNames =
    foundation::table<Phrase>(foundation::is<Phrase::ImplementationDefined>("implementation-defined"sv),
                              foundation::is<Phrase::Ntbs>("ntbs"sv),
                              foundation::is<Phrase::Ntmbs>("ntmbs"sv));

inline constexpr auto kPhraseWords =
    foundation::table<Phrase>(foundation::is<Phrase::ImplementationDefined>("implementation-defined"sv),
                              foundation::is<Phrase::Ntbs>("NTBS"sv),
                              foundation::is<Phrase::Ntmbs>("NTMBS"sv));

inline constexpr auto kChangeNames = foundation::table<Change>(foundation::is<Change::Same>("same"sv),
                                                               foundation::is<Change::Added>("added"sv),
                                                               foundation::is<Change::Removed>("removed"sv),
                                                               foundation::is<Change::Elided>("elided"sv));

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

} // namespace

std::string_view        span_kind_name(SpanKind k) { return kSpanKindNames[k]; }
std::optional<SpanKind> span_kind_from_name(std::string_view s) { return enumerator_named(kSpanKindNames, s); }

std::string_view         index_kind_name(IndexKind k) { return kIndexKindNames[k]; }
std::optional<IndexKind> index_kind_from_name(std::string_view s) { return enumerator_named(kIndexKindNames, s); }

std::string_view           element_name(ElementKind k) { return kElementNames[k]; }
std::optional<ElementKind> element_from_name(std::string_view s) { return enumerator_named(kElementNames, s); }

std::string_view           disposition_name(Disposition d) { return kDispositionNames[d]; }
std::optional<Disposition> disposition_from_name(std::string_view s) { return enumerator_named(kDispositionNames, s); }

std::string_view          member_kind_name(MemberKind k) { return kMemberKindNames[k]; }
std::optional<MemberKind> member_kind_from_name(std::string_view s) { return enumerator_named(kMemberKindNames, s); }

std::string_view              layout_hint_name(LayoutHintKind k) { return kLayoutHintNames[k]; }
std::optional<LayoutHintKind> layout_hint_from_name(std::string_view s) {
    return enumerator_named(kLayoutHintNames, s);
}

std::string_view        term_role_name(TermRole r) { return kTermRoleNames[r]; }
std::optional<TermRole> term_role_from_name(std::string_view s) { return enumerator_named(kTermRoleNames, s); }

std::string_view      phrase_name(Phrase p) { return kPhraseNames[p]; }
std::optional<Phrase> phrase_from_name(std::string_view s) { return enumerator_named(kPhraseNames, s); }
std::string_view      phrase_words(Phrase p) { return kPhraseWords[p]; }

std::string_view      change_name(Change c) { return kChangeNames[c]; }
std::optional<Change> change_from_name(std::string_view s) { return enumerator_named(kChangeNames, s); }

namespace {

// `change_of`'s dispatch (decision visitation-rules: named struct, Node has
// four alternatives).
struct ChangeOf {
    Change operator()(const Section& s) const { return s.change; }
    Change operator()(const Synopsis& s) const { return s.change; }
    Change operator()(const SpecItem& s) const { return s.change; }
    Change operator()(const FreeParagraph& s) const { return s.change; }
};

// `with_change`'s dispatch, consuming its argument by move.
struct WithChange {
    Change change;

    Node operator()(Section&& s) const {
        s.change = change;
        return std::move(s);
    }
    Node operator()(Synopsis&& s) const {
        s.change = change;
        return std::move(s);
    }
    Node operator()(SpecItem&& s) const {
        s.change = change;
        return std::move(s);
    }
    Node operator()(FreeParagraph&& s) const {
        s.change = change;
        return std::move(s);
    }
};

} // namespace

Change change_of(const Node& node) { return std::visit(foundation::overloaded{ChangeOf{}}, node); }

namespace {

// `block_change`'s dispatch over either body variant: the eight block kinds
// each carry a `change`, and the five informative ones are a subset.
struct BlockChange {
    Change operator()(const BodyParagraph& b) const { return b.change; }
    Change operator()(const Itemize& b) const { return b.change; }
    Change operator()(const Table2D& b) const { return b.change; }
    Change operator()(const Table1D& b) const { return b.change; }
    Change operator()(const EquivalentTo& b) const { return b.change; }
    Change operator()(const AuthoredCode& b) const { return b.change; }
    Change operator()(const Note& b) const { return b.change; }
    Change operator()(const Example& b) const { return b.change; }
};

} // namespace

Change block_change(const BodyBlock& block) { return std::visit(foundation::overloaded{BlockChange{}}, block); }
Change block_change(const InformativeBlock& block) { return std::visit(foundation::overloaded{BlockChange{}}, block); }

namespace {

// `opens_paragraph`'s dispatch over the eight block kinds.
struct OpensParagraph {
    bool operator()(const BodyParagraph& b) const { return !b.continuation; }
    bool operator()(const Itemize& b) const { return b.opens_paragraph; }
    bool operator()(const Table2D& b) const { return b.opens_paragraph; }
    bool operator()(const Table1D& b) const { return b.opens_paragraph; }
    bool operator()(const EquivalentTo&) const { return true; }
    bool operator()(const AuthoredCode& b) const { return b.opens_paragraph; }
    bool operator()(const Note& b) const { return b.opens_paragraph; }
    bool operator()(const Example& b) const { return b.opens_paragraph; }
};

} // namespace

bool opens_paragraph(const BodyBlock& block) { return std::visit(foundation::overloaded{OpensParagraph{}}, block); }

Node with_change(Node node, Change change) {
    return std::visit(foundation::overloaded{WithChange{change}}, std::move(node));
}

std::vector<Node> all_marked(std::vector<Node> nodes, Change change) {
    std::ranges::transform(
        nodes, nodes.begin(), [change](Node& node) { return with_change(std::move(node), change); });
    return nodes;
}

namespace {
struct LeafParagraphs {
    std::vector<const Paragraph*> operator()(const BodyParagraph& p) const;
    std::vector<const Paragraph*> operator()(const Itemize& list) const;
    std::vector<const Paragraph*> operator()(const Table2D& t) const;
    std::vector<const Paragraph*> operator()(const Table1D& t) const;
    std::vector<const Paragraph*> operator()(const AuthoredCode&) const;
    // A list item's blocks are a description body's (issue #389).
    std::vector<const Paragraph*> operator()(const EquivalentTo&) const { return {}; }
    std::vector<const Paragraph*> operator()(const Note& note) const { return body_leaves(note.body); }
    std::vector<const Paragraph*> operator()(const Example& example) const { return body_leaves(example.body); }
    std::vector<const Paragraph*> body_leaves(const InformativeBody& body) const {
        return body |
               std::views::transform([this](const InformativeBlock& block) { return std::visit(*this, block); }) |
               std::views::join | std::ranges::to<std::vector>();
    }
};

std::vector<const Paragraph*> LeafParagraphs::operator()(const BodyParagraph& p) const { return {&p.text}; }

// An item's own paragraph, then the leaves of the blocks after it.
std::vector<const Paragraph*> LeafParagraphs::operator()(const Itemize& list) const {
    return list.items | std::views::transform([this](const ListItem& item) {
               std::vector<const Paragraph*> leaves{&item.text};
               leaves.append_range(item.blocks | std::views::transform([this](const BodyBlock& block) {
                                       return std::visit(*this, block);
                                   }) |
                                   std::views::join);
               return leaves;
           }) |
           std::views::join | std::ranges::to<std::vector>();
}

std::vector<const Paragraph*> LeafParagraphs::operator()(const Table2D& t) const {
    std::vector<const Paragraph*> result{&t.caption, &t.column1, &t.column2};
    result.append_range(t.rows | std::views::transform([](const Table2DRow& row) {
                            return std::array{&row.header, &row.cell1, &row.cell2};
                        }) |
                        std::views::join);
    return result;
}

std::vector<const Paragraph*> LeafParagraphs::operator()(const Table1D& t) const {
    std::vector<const Paragraph*> result{&t.caption, &t.column1, &t.column2};
    result.append_range(
        t.rows | std::views::transform([](const Table1DRow& row) { return std::array{&row.cell1, &row.cell2}; }) |
        std::views::join);
    return result;
}

std::vector<const Paragraph*> LeafParagraphs::operator()(const AuthoredCode&) const { return {}; }

std::vector<const Paragraph*> informative_paragraphs(const InformativeBody& body);
std::vector<const CodeText*>  informative_code(const InformativeBody& body);

struct BlockParagraphs : LeafParagraphs {
    using LeafParagraphs::operator();
    bool                          include_informative;
    std::vector<const Paragraph*> operator()(const EquivalentTo&) const;
    std::vector<const Paragraph*> operator()(const Note& note) const;
    std::vector<const Paragraph*> operator()(const Example& example) const;
};
std::vector<const Paragraph*> BlockParagraphs::operator()(const EquivalentTo&) const { return {}; }
std::vector<const Paragraph*> BlockParagraphs::operator()(const Note& note) const {
    return include_informative ? informative_paragraphs(note.body) : std::vector<const Paragraph*>{};
}
std::vector<const Paragraph*> BlockParagraphs::operator()(const Example& example) const {
    return include_informative ? informative_paragraphs(example.body) : std::vector<const Paragraph*>{};
}

struct LeafCode {
    std::vector<const CodeText*> operator()(const BodyParagraph&) const;
    std::vector<const CodeText*> operator()(const Itemize&) const;
    std::vector<const CodeText*> operator()(const Table2D&) const;
    std::vector<const CodeText*> operator()(const Table1D&) const;
    std::vector<const CodeText*> operator()(const AuthoredCode& code) const;
};
std::vector<const CodeText*> LeafCode::operator()(const BodyParagraph&) const { return {}; }
std::vector<const CodeText*> LeafCode::operator()(const Itemize&) const { return {}; }
std::vector<const CodeText*> LeafCode::operator()(const Table2D&) const { return {}; }
std::vector<const CodeText*> LeafCode::operator()(const Table1D&) const { return {}; }
std::vector<const CodeText*> LeafCode::operator()(const AuthoredCode& code) const { return {&code.code}; }

struct BlockCode : LeafCode {
    using LeafCode::operator();
    bool                         include_informative;
    std::vector<const CodeText*> operator()(const EquivalentTo& code) const;
    std::vector<const CodeText*> operator()(const Note& note) const;
    std::vector<const CodeText*> operator()(const Example& example) const;
};
std::vector<const CodeText*> BlockCode::operator()(const EquivalentTo& code) const { return {&code.code}; }
std::vector<const CodeText*> BlockCode::operator()(const Note& note) const {
    return include_informative ? informative_code(note.body) : std::vector<const CodeText*>{};
}
std::vector<const CodeText*> BlockCode::operator()(const Example& example) const {
    return include_informative ? informative_code(example.body) : std::vector<const CodeText*>{};
}

std::vector<const Paragraph*> informative_paragraphs(const InformativeBody& body) {
    return body | std::views::transform([](const InformativeBlock& block) {
               return std::visit(foundation::overloaded{LeafParagraphs{}}, block);
           }) |
           std::views::join | std::ranges::to<std::vector>();
}
std::vector<const CodeText*> informative_code(const InformativeBody& body) {
    return body | std::views::transform([](const InformativeBlock& block) {
               return std::visit(foundation::overloaded{LeafCode{}}, block);
           }) |
           std::views::join | std::ranges::to<std::vector>();
}
} // namespace

std::vector<const Paragraph*> body_paragraphs(const DescriptionBody& body, bool include_informative) {
    return body | std::views::transform([include_informative](const BodyBlock& block) {
               return std::visit(foundation::overloaded{BlockParagraphs{{}, include_informative}}, block);
           }) |
           std::views::join | std::ranges::to<std::vector>();
}
std::vector<const CodeText*> body_code(const DescriptionBody& body, bool include_informative) {
    return body | std::views::transform([include_informative](const BodyBlock& block) {
               return std::visit(foundation::overloaded{BlockCode{{}, include_informative}}, block);
           }) |
           std::views::join | std::ranges::to<std::vector>();
}

DescriptionBody coalesce_itemizations(DescriptionBody body) {
    return std::ranges::fold_left(
        std::move(body) | std::views::as_rvalue, DescriptionBody{}, [](DescriptionBody out, BodyBlock block) {
            auto* next = std::get_if<Itemize>(&block);
            auto* last = out.empty() ? nullptr : std::get_if<Itemize>(&out.back());
            if (next != nullptr && last != nullptr && !next->opens_paragraph)
                last->items.append_range(std::move(next->items) | std::views::as_rvalue);
            else
                out.push_back(std::move(block));
            return out;
        });
}

TextInline text_piece(const TextInline& text, std::size_t begin, std::size_t end) {
    const bool last = end >= text.text.size();
    return TextInline{text.text.substr(begin, end - begin), text.hints | std::views::filter([&](const LayoutHint& h) {
                                                                return h.at >= begin &&
                                                                       (h.at < end || (last && h.at == end));
                                                            }) | std::views::transform([&](LayoutHint h) {
                                                                h.at -= begin;
                                                                return h;
                                                            }) | std::ranges::to<std::vector>()};
}

void append_text(TextInline& text, const TextInline& more) {
    const std::size_t shift = text.text.size();
    text.hints.append_range(more.hints | std::views::transform([&](LayoutHint h) {
                                h.at += shift;
                                return h;
                            }));
    text.text += more.text;
}

CodeText without_typesetting(CodeText code) {
    std::erase_if(code.spans, [](const Span& span) { return !is_wording(span); });
    std::ranges::for_each(code.spans, [](Span& span) { span.no_correction.reset(); });
    code.hints.clear();
    return code;
}

namespace {

// `without_typesetting`'s dispatch over the plain inline kinds: the five that
// carry typesetting lose it, and the rest are returned as they came.
struct TypesettingRemover {
    PlainInline operator()(TextInline v) const {
        v.hints.clear();
        return v;
    }
    PlainInline operator()(CodeInline v) const { return CodeInline{without_typesetting(std::move(v.code))}; }
    PlainInline operator()(RefInline v) const { return v; }
    PlainInline operator()(ConceptRef v) const {
        v.no_correction = false;
        v.in_tcode      = false;
        v.hints.clear();
        return v;
    }
    PlainInline operator()(PlaceholderInline v) const {
        v.no_correction = false;
        v.hints.clear();
        return v;
    }
    PlainInline operator()(OldConceptRef v) const {
        v.hints.clear();
        return v;
    }
    PlainInline operator()(TableRef v) const { return v; }
    PlainInline operator()(RawInline v) const { return v; }
    PlainInline operator()(TermInline v) const {
        std::ranges::for_each(v.content, [](TermPiece& piece) {
            if (auto* text = std::get_if<TextInline>(&piece))
                text->hints.clear();
            else
                std::get<CodeInline>(piece).code = without_typesetting(std::move(std::get<CodeInline>(piece).code));
        });
        return v;
    }
    // An index entry is not wording (`is_wording`).
    PlainInline operator()(PhraseInline v) const {
        v.index.clear();
        v.bare = false;
        return v;
    }
};

} // namespace

PlainInline without_typesetting(PlainInline piece) { return std::visit(TypesettingRemover{}, std::move(piece)); }

bool has_description(const ItemDescr& d) { return !d.leading.empty() || !d.elements.empty(); }

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

} // namespace beman::specgen::ir
