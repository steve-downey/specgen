// src/beman/specgen/inherit.cpp                                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/inherit.hpp>

#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/diff.hpp>
#include <beman/specgen/foundation/overloaded.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/synopsis_check.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace beman::specgen::inherit {

namespace {

using foundation::overloaded;

std::string first_signature(const ir::SpecItem& item) {
    return item.decl.signatures.empty() ? std::string("a description with no declaration")
                                        : item.decl.signatures.front().text;
}

std::vector<std::string> sorted_entities(const ir::SpecItem& item) {
    // Distinct: an `\also` group names its entity once per signature, and the
    // draft's index names it once.
    std::vector<std::string> names = item.decl.entities;
    std::ranges::sort(names);
    names.erase(std::ranges::unique(names).begin(), names.end());
    return names;
}

// How alike two items' declarations read: the best match over their signatures,
// so an overload set pairs with the one that reads most like it.
double declared_alike(const ir::SpecItem& a, const ir::SpecItem& b) {
    // A description with no declaration -- a class's own wording, or a
    // section's detached prose -- pairs with one, in order.
    if (a.decl.signatures.empty() || b.decl.signatures.empty())
        return a.decl.signatures.empty() && b.decl.signatures.empty() ? 1.0 : 0.0;
    const auto scores = std::views::cartesian_product(a.decl.signatures, b.decl.signatures) |
                        std::views::transform([](const auto& pair) {
                            const auto& [x, y] = pair;
                            return x == y ? 1.0 : diff::declaration_similarity(x, y);
                        });
    return std::ranges::fold_left(scores, 0.0, [](double best, double score) { return std::max(best, score); });
}

const ir::Section* find_section(const std::vector<ir::Node>& nodes, const std::string& stable_name) {
    const auto found = std::ranges::find_if(nodes, [&](const ir::Node& node) {
        const auto* section = std::get_if<ir::Section>(&node);
        return section != nullptr && section->stable_name == stable_name;
    });
    return found == nodes.end() ? nullptr : std::get_if<ir::Section>(&*found);
}

// The baseline item `item` takes its description from: among the unclaimed
// items declaring the same names, the one whose signature reads most like its
// own. Nothing reading alike at all is no pair.
const ir::SpecItem*
pair_item(const ir::SpecItem& item, const std::vector<ir::Node>& baseline, std::vector<bool>& claimed) {
    const std::vector<std::string> names = sorted_entities(item);
    std::optional<std::size_t>     best;
    double                         best_score = 0.0;
    // substrate generic algorithm: an argmax over the baseline's items, taking
    // the first of equals, which writes the claim table.
    for (std::size_t i = 0; i < baseline.size(); ++i) {
        const auto* candidate = std::get_if<ir::SpecItem>(&baseline[i]);
        if (candidate == nullptr || claimed[i] || sorted_entities(*candidate) != names)
            continue;
        const double score = declared_alike(item, *candidate);
        if (score > best_score) {
            best       = i;
            best_score = score;
        }
    }
    if (!best)
        return nullptr;
    claimed[*best] = true;
    return std::get_if<ir::SpecItem>(&baseline[*best]);
}

// The description `item` ends with, given the baseline's.
void inherit_description(ir::SpecItem& item, const ir::SpecItem& from, std::vector<Diagnostic>& out) {
    ir::ItemDescr resolved = from.descr;
    // substrate generic algorithm: each of the item's own elements acts on the
    // baseline's element of its kind, which mutates the description built so far.
    for (ir::DescriptionElement& own : item.descr.elements) {
        const auto same_kind = std::ranges::find_if(
            resolved.elements, [&](const ir::DescriptionElement& e) { return e.kind == own.kind; });
        if (own.derived) {
            if (same_kind != resolved.elements.end()) {
                // The code's own derivation does not replace the draft's words;
                // it is what they are checked against.
                same_kind->conjuncts = std::move(own.conjuncts);
                same_kind->derived   = false;
            } else {
                resolved.elements.push_back(std::move(own));
            }
        } else if (same_kind != resolved.elements.end()) {
            *same_kind = std::move(own);
        } else {
            resolved.elements.push_back(std::move(own));
        }
    }
    if (!item.descr.leading.empty())
        resolved.leading = std::move(item.descr.leading);

    // substrate generic algorithm: each claim lands on the element it names.
    for (const ir::InheritedCover& cover : item.covers) {
        const auto target = std::ranges::find_if(
            resolved.elements, [&](const ir::DescriptionElement& e) { return e.kind == cover.kind; });
        if (target == resolved.elements.end())
            out.push_back({Severity::Error,
                           first_signature(item),
                           std::format("\\covers names the {} element, and neither the baseline's description nor "
                                       "this item has one",
                                       ir::element_name(cover.kind))});
        else
            target->coverage.push_back(cover.claim);
    }
    ir::canonicalize(resolved);
    item.descr    = std::move(resolved);
    item.as_draft = false;
    item.covers.clear();
}

// The baseline's synopsis of the same class that nothing has claimed.
const ir::Synopsis*
pair_synopsis(const ir::Synopsis& synopsis, const std::vector<ir::Node>& baseline, std::vector<bool>& claimed) {
    // substrate generic algorithm: a search that writes the claim table at the hit.
    for (std::size_t i = 0; i < baseline.size(); ++i) {
        const auto* candidate = std::get_if<ir::Synopsis>(&baseline[i]);
        if (candidate != nullptr && !claimed[i] && candidate->name == synopsis.name) {
            claimed[i] = true;
            return candidate;
        }
    }
    return nullptr;
}

void resolve_synopsis(ir::Synopsis& synopsis, const ir::Synopsis& from, std::vector<Diagnostic>& out) {
    // Without the class's own declarations there is nothing to patch from; the
    // baseline's text stands.
    ir::CodeText code = from.code;
    if (synopsis.declared) {
        synopsis_check::Patched patched = synopsis_check::patch(from.code, *synopsis.declared);
        code                            = std::move(patched.code);
        // substrate generic algorithm: formats each unpatched finding onto the result.
        for (const synopsis_check::Finding& finding : patched.unpatched)
            out.push_back({finding.severity,
                           synopsis.name.empty() ? std::string("synopsis") : synopsis.name + "/synopsis",
                           std::format("`{}`: {}; the inherited synopsis cannot be patched to agree, so write it with "
                                       "\\verbatim-synopsis",
                                       finding.declaration,
                                       finding.message)});
    }
    synopsis.code     = std::move(code);
    synopsis.as_draft = false;
}

void resolve_nodes(std::vector<ir::Node>&       current,
                   const std::vector<ir::Node>& baseline,
                   const std::string&           where,
                   std::vector<Diagnostic>&     out) {
    std::vector<bool> claimed(baseline.size(), false);
    // substrate generic algorithm: nodes of different kinds resolve differently,
    // and an item's claim on a baseline item is state shared across the siblings.
    for (ir::Node& node : current) {
        std::visit(overloaded{
                       [&](ir::Section& section) {
                           const ir::Section* paired = find_section(baseline, section.stable_name);
                           if (paired != nullptr) {
                               resolve_nodes(section.children, paired->children, section.stable_name, out);
                               return;
                           }
                           // A section the baseline lacks has nothing to inherit; an item in
                           // it that asks to is reported by the walk with an empty baseline.
                           resolve_nodes(section.children, {}, section.stable_name, out);
                       },
                       [&](ir::SpecItem& item) {
                           if (!item.as_draft)
                               return;
                           const ir::SpecItem* from = pair_item(item, baseline, claimed);
                           if (from == nullptr) {
                               out.push_back({Severity::Error,
                                              first_signature(item),
                                              std::format("\\asdraft: no item of the baseline{}{} declares this",
                                                          where.empty() ? "" : "'s section ",
                                                          where)});
                               return;
                           }
                           inherit_description(item, *from, out);
                       },
                       [&](ir::Synopsis& synopsis) {
                           if (!synopsis.as_draft)
                               return;
                           const ir::Synopsis* from = pair_synopsis(synopsis, baseline, claimed);
                           if (from == nullptr) {
                               out.push_back(
                                   {Severity::Error,
                                    synopsis.name + "/synopsis",
                                    std::format("\\asdraft-synopsis: no synopsis of `{}` in the baseline{}{}",
                                                synopsis.name,
                                                where.empty() ? "" : "'s section ",
                                                where)});
                               return;
                           }
                           resolve_synopsis(synopsis, *from, out);
                       },
                       [](ir::FreeParagraph&) {},
                   },
                   node);
    }
}

bool any_unresolved(const std::vector<ir::Node>& nodes) {
    return std::ranges::any_of(nodes, [](const ir::Node& node) {
        return std::visit(overloaded{
                              [](const ir::Section& section) { return any_unresolved(section.children); },
                              [](const ir::SpecItem& item) { return item.as_draft; },
                              [](const ir::Synopsis& synopsis) { return synopsis.as_draft; },
                              [](const ir::FreeParagraph&) { return false; },
                          },
                          node);
    });
}

} // namespace

bool has_unresolved(const ir::Document& document) { return any_unresolved(document.nodes); }

std::vector<Diagnostic> resolve(ir::Document& current, const ir::Document& baseline) {
    std::vector<Diagnostic> out;
    resolve_nodes(current.nodes, baseline.nodes, {}, out);
    return out;
}

} // namespace beman::specgen::inherit
