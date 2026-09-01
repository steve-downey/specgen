// src/beman/specgen/lower.cpp                                      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/lower.hpp>

#include <beman/specgen/foundation/overloaded.hpp>

#include <algorithm>
#include <ranges>
#include <utility>
#include <variant>

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
                   },
                   piece);
           }) |
           std::ranges::to<ir::Paragraph>();
}

ir::DescriptionElement lower_element(const grammar::Element& element) {
    ir::DescriptionElement lowered;
    lowered.kind       = element.kind;
    lowered.paragraphs = element.paragraphs | std::views::transform(lower_paragraph) | std::ranges::to<std::vector>();
    if (!element.items.empty())
        lowered.itemize =
            ir::Itemize{element.items | std::views::transform(lower_paragraph) | std::ranges::to<std::vector>()};
    if (element.table) {
        lowered.table = ir::Table2D{
            .stable_name = element.table->stable_name,
            .caption     = lower_paragraph(element.table->caption),
            .column1     = lower_paragraph(element.table->column1),
            .column2     = lower_paragraph(element.table->column2),
            .rows        = element.table->rows | std::views::transform([](const grammar::Table2DRow& row) {
                        return ir::Table2DRow{.header = lower_paragraph(row.header),
                                              .cell1  = lower_paragraph(row.cell1),
                                              .cell2  = lower_paragraph(row.cell2)};
                           }) |
                           std::ranges::to<std::vector>(),
        };
    }
    return lowered;
}

// An extraction marker contributes an element whose prose is empty and whose
// code is a placeholder; the front end fills the code from the definition body.
void append_extraction(ir::ItemDescr& descr, ir::ElementKind kind) {
    ir::DescriptionElement element;
    element.kind       = kind;
    element.equivalent = ir::EquivalentTo{};
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

    out.descr.elements = block.elements | std::views::transform(lower_element) | std::ranges::to<std::vector>();

    // The grammar rejects \effects with \effects-equiv (and the Returns pair),
    // so these cannot collide with an authored element of the same kind.
    if (block.markers.effects_equiv)
        append_extraction(out.descr, ir::ElementKind::Effects);
    if (block.markers.returns_equiv)
        append_extraction(out.descr, ir::ElementKind::Returns);

    // ItemDirectives is grammar::Markers (decision marker-registry): a plain
    // copy, not a field-by-field transcription that could drift from it.
    out.directives = block.markers;

    // Authored order is irrelevant; [structure.specifications] order is not.
    ir::canonicalize(out.descr);
    return out;
}

} // namespace beman::specgen::lowering
