// src/beman/specgen/conjuncts.cpp                                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/conjuncts.hpp>

#include <ranges>
#include <utility>

namespace beman::specgen::conjuncts {

namespace {

void append(ir::Paragraph& out, const ir::Paragraph& piece) { out.insert(out.end(), piece.begin(), piece.end()); }

void append_text(ir::Paragraph& out, std::string text) { out.push_back(ir::TextInline{std::move(text)}); }

} // namespace

ir::Paragraph join_sentence(const std::vector<ir::Paragraph>& parts) {
    if (parts.empty())
        return {};

    const std::size_t total = parts.size();

    // The separator before a part depends on its position, not just its
    // identity: nothing before the first part, a bare " and " between exactly
    // two, and a serial comma before the last of three or more. That
    // positional dependence is why the parts and their separators are joined
    // via enumerate + transform rather than a plain views::join_with.
    ir::Paragraph out = parts | std::views::enumerate | std::views::transform([total](const auto& indexed) {
                            const auto& [i, part] = indexed;
                            ir::Paragraph chunk;
                            if (i > 0) {
                                if (total == 2)
                                    append_text(chunk, " and ");
                                else if (static_cast<std::size_t>(i) + 1 == total)
                                    append_text(chunk, ", and "); // serial comma, as the draft writes it
                                else
                                    append_text(chunk, ", ");
                            }
                            append(chunk, part);
                            return chunk;
                        }) |
                        std::views::join | std::ranges::to<ir::Paragraph>();
    append_text(out, ".");
    return out;
}

ir::Itemize as_itemize(const std::vector<ir::Paragraph>& parts) {
    const std::size_t total = parts.size();

    ir::Itemize itemize;
    itemize.items = parts | std::views::enumerate | std::views::transform([total](const auto& indexed) {
                        const auto& [i, part] = indexed;
                        ir::Paragraph item    = part;
                        append_text(item, static_cast<std::size_t>(i) + 1 == total ? "." : ",");
                        return item;
                    }) |
                    std::ranges::to<std::vector>();
    return itemize;
}

void render_into(const std::vector<ir::Paragraph>& parts, ir::DescriptionElement& element, const Options& options) {
    if (parts.empty())
        return;

    if (parts.size() <= options.sentence_threshold)
        element.paragraphs.push_back(join_sentence(parts));
    else
        element.itemize = as_itemize(parts);
}

} // namespace beman::specgen::conjuncts
