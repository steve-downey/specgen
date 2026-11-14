// src/beman/specgen/lower.cpp                                      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/lower.hpp>

#include <beman/specgen/docblock.hpp>
#include <beman/specgen/foundation/overloaded.hpp>
#include <beman/specgen/ir.hpp>

#include <algorithm>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace beman::specgen::lowering {

namespace {

using beman::specgen::foundation::overloaded;

ir::Paragraph lower_paragraph(const grammar::ProseParagraph& para) {
    return para | std::views::transform([](const grammar::ProseInline& piece) -> ir::Inline {
               return std::visit(
                   overloaded{
                       [](const grammar::InlineText& text) -> ir::Inline { return ir::TextInline{text.text}; },
                       [](const grammar::InlineCode& code) -> ir::Inline {
                           // Empty span table: names are raw until the front end resolves them.
                           return ir::CodeInline{ir::CodeText{code.code, {}}};
                       },
                       [](const grammar::InlineRef& ref) -> ir::Inline { return ir::RefInline{ref.stable_name}; },
                       [](const grammar::InlinePlaceholder& p) -> ir::Inline { return ir::PlaceholderInline{p.text}; },
                       [](const grammar::InlineOldConcept& c) -> ir::Inline { return ir::OldConceptRef{c.name}; },
                   },
                   piece);
           }) |
           std::ranges::to<ir::Paragraph>();
}

template <class Block>
struct LeafBodyLowerer {
    Block operator()(const grammar::BodyParagraph&) const;
    Block operator()(const grammar::Itemize&) const;
    Block operator()(const grammar::Table2D&) const;
    Block operator()(const grammar::Table1D&) const;
    Block operator()(const grammar::AuthoredCode&) const;
};

template <class Block>
Block LeafBodyLowerer<Block>::operator()(const grammar::BodyParagraph& p) const {
    return ir::BodyParagraph{lower_paragraph(p.text), p.continuation};
}
template <class Block>
Block LeafBodyLowerer<Block>::operator()(const grammar::Itemize& list) const {
    return ir::Itemize{list.items | std::views::transform(lower_paragraph) | std::ranges::to<std::vector>()};
}
template <class Block>
Block LeafBodyLowerer<Block>::operator()(const grammar::Table2D& table) const {
    return ir::Table2D{.stable_name = table.stable_name,
                       .caption     = lower_paragraph(table.caption),
                       .column1     = lower_paragraph(table.column1),
                       .column2     = lower_paragraph(table.column2),
                       .rows        = table.rows | std::views::transform([](const grammar::Table2DRow& row) {
                                   return ir::Table2DRow{lower_paragraph(row.header),
                                                         lower_paragraph(row.cell1),
                                                         lower_paragraph(row.cell2)};
                                      }) |
                                      std::ranges::to<std::vector>()};
}
template <class Block>
Block LeafBodyLowerer<Block>::operator()(const grammar::Table1D& table) const {
    return ir::Table1D{.stable_name = table.stable_name,
                       .caption     = lower_paragraph(table.caption),
                       .column1     = lower_paragraph(table.column1),
                       .column2     = lower_paragraph(table.column2),
                       .rows        = table.rows | std::views::transform([](const grammar::Table1DRow& row) {
                                   return ir::Table1DRow{lower_paragraph(row.cell1), lower_paragraph(row.cell2)};
                                      }) |
                                      std::ranges::to<std::vector>()};
}

template <class Block>
Block LeafBodyLowerer<Block>::operator()(const grammar::AuthoredCode& code) const {
    return ir::AuthoredCode{code.code};
}

ir::InformativeBody lower_informative_body(const grammar::InformativeBody&);
ir::InformativeBody lower_informative_body(const grammar::InformativeBody& body) {
    return body | std::views::transform([](const grammar::InformativeBlock& block) {
               return std::visit(overloaded{LeafBodyLowerer<ir::InformativeBlock>{}}, block);
           }) |
           std::ranges::to<ir::InformativeBody>();
}

struct BodyLowerer : LeafBodyLowerer<ir::BodyBlock> {
    using LeafBodyLowerer<ir::BodyBlock>::operator();
    ir::BodyBlock operator()(const grammar::Note&) const;
    ir::BodyBlock operator()(const grammar::Example&) const;
};

ir::BodyBlock BodyLowerer::operator()(const grammar::Note& note) const {
    return ir::Note{lower_informative_body(note.body)};
}
ir::BodyBlock BodyLowerer::operator()(const grammar::Example& example) const {
    return ir::Example{lower_informative_body(example.body)};
}

ir::DescriptionElement lower_element(const grammar::Element& element) {
    return ir::DescriptionElement{.kind    = element.kind,
                                  .body    = element.body | std::views::transform([](const grammar::BodyBlock& block) {
                                              return std::visit(overloaded{BodyLowerer{}}, block);
                                             }) |
                                             std::ranges::to<ir::DescriptionBody>(),
                                  .derived = false,
                                  .conjuncts = {},
                                  .coverage  = element.coverage | std::views::transform(lower_paragraph) |
                                               std::ranges::to<std::vector>()};
}

// An extraction marker contributes an element whose prose is empty and whose
// code is a placeholder; the front end fills the code from the definition body.
void append_extraction(ir::ItemDescr& descr, ir::ElementKind kind) {
    ir::DescriptionElement element;
    element.kind = kind;
    element.body.push_back(ir::EquivalentTo{});
    descr.elements.push_back(std::move(element));
}

} // namespace

std::string exposid_name(std::string_view identifier) {
    std::string name(identifier.substr(0, identifier.find_last_not_of('_') + 1));
    std::ranges::replace(name, '_', '-');
    return name;
}

Lowered lower(const grammar::Docblock& block) {
    Lowered out;

    out.descr.leading = block.leading | std::views::transform([](const grammar::BodyBlock& block) {
                            return std::visit(overloaded{BodyLowerer{}}, block);
                        }) |
                        std::ranges::to<ir::DescriptionBody>();

    out.descr.elements = block.elements | std::views::transform(lower_element) | std::ranges::to<std::vector>();

    // The grammar rejects \effects with \effects-equiv, so this cannot collide
    // with an authored element of the same kind. \returns-expr leaves no
    // placeholder: its element is a bare expression, not an Equivalent to:
    // block, and only the front end can tell whether the body is one
    // `return E;` (issue #143).
    if (block.markers.effects_equiv)
        append_extraction(out.descr, ir::ElementKind::Effects);

    // ItemDirectives is grammar::Markers (decision marker-registry): a plain
    // copy, not a field-by-field transcription that could drift from it.
    out.directives = block.markers;

    // Authored order is irrelevant; [structure.specifications] order is not.
    ir::canonicalize(out.descr);
    return out;
}

} // namespace beman::specgen::lowering
