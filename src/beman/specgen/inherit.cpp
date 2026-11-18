// src/beman/specgen/inherit.cpp                                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/inherit.hpp>

#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/diff.hpp>
#include <beman/specgen/draft_import.hpp>
#include <beman/specgen/foundation/overloaded.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/synopsis_check.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <functional>
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
// A signature typed in a header keeps its `@\exposid{x}@` escapes as written;
// the draft's, imported, have them as spans. Read the typed one the way the
// importer reads a listing, so the same declaration reads alike.
ir::CodeText as_imported(const ir::CodeText& code) {
    return code.spans.empty() && code.text.contains('@') ? draft_import::parse_listing(code.text) : code;
}

double declared_alike(const ir::SpecItem& a, const ir::SpecItem& b) {
    // A description with no declaration -- a class's own wording, or a
    // section's detached prose -- pairs with one, in order.
    if (a.decl.signatures.empty() || b.decl.signatures.empty())
        return a.decl.signatures.empty() && b.decl.signatures.empty() ? 1.0 : 0.0;
    const auto scores = std::views::cartesian_product(a.decl.signatures, b.decl.signatures) |
                        std::views::transform([](const auto& pair) {
                            const auto& [x, y] = pair;
                            return x == y ? 1.0 : diff::declaration_similarity(as_imported(x), as_imported(y));
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

// How alike two declarations must read to pair when one side gives no names
// to pair by: the differ's own measure and threshold for a pair.
constexpr double kReadingThreshold = 0.5;

// The unclaimed baseline item among the eligible ones whose signature reads
// most like `item`'s, the first of equals, reading at least `minimum` alike.
template <class Eligible>
std::optional<std::size_t> best_reading(const ir::SpecItem&          item,
                                        const std::vector<ir::Node>& baseline,
                                        const std::vector<bool>&     claimed,
                                        double                       minimum,
                                        Eligible                     eligible) {
    const auto scored = std::views::iota(std::size_t{0}, baseline.size()) | std::views::filter([&](std::size_t i) {
                            const auto* candidate = std::get_if<ir::SpecItem>(&baseline[i]);
                            return candidate != nullptr && !claimed[i] && eligible(*candidate);
                        }) |
                        std::views::transform([&](std::size_t i) {
                            return std::pair{i, declared_alike(item, std::get<ir::SpecItem>(baseline[i]))};
                        }) |
                        std::views::filter([&](const auto& entry) { return entry.second >= minimum; }) |
                        std::ranges::to<std::vector>();
    const auto best =
        std::ranges::max_element(scored, std::ranges::less{}, [](const auto& entry) { return entry.second; });
    return best == scored.end() || best->second <= 0.0 ? std::nullopt : std::optional(best->first);
}

// The baseline item `item` takes its description from: among the unclaimed
// items declaring the same names, the one whose signature reads most like its
// own. Nothing reading alike at all is no pair.
//
// Failing that, the pair is made by how the declarations read, as the differ
// makes it, when one side gives no names to pair by. An item typed as a
// `\verbatim-itemdecl` names nothing the front end knows of. And the draft's
// index says nothing of an exposition-only member, so its item for
// `is-callable-from` has no names where the header's has `is_callable_from`
// (issue #328).
const ir::SpecItem*
pair_item(const ir::SpecItem& item, const std::vector<ir::Node>& baseline, std::vector<bool>& claimed) {
    const std::vector<std::string> names    = sorted_entities(item);
    const bool                     declares = !item.decl.signatures.empty();
    std::optional<std::size_t>     best;
    if (!names.empty() || !declares)
        best = best_reading(item, baseline, claimed, 0.0, [&](const ir::SpecItem& candidate) {
            return sorted_entities(candidate) == names;
        });
    if (!best && declares)
        best = best_reading(item, baseline, claimed, kReadingThreshold, [&](const ir::SpecItem& candidate) {
            return !candidate.decl.signatures.empty() && (names.empty() || candidate.decl.entities.empty());
        });
    if (!best)
        return nullptr;
    claimed[*best] = true;
    return std::get_if<ir::SpecItem>(&baseline[*best]);
}

// Which of its macro's two forms each span of `code` is written in, taken from
// the span of `from` that says the same thing: the same kind and payload, the
// first with the first. Only a span the document left to its context's default
// takes one, so a form set by hand stays.
void take_form(ir::CodeText& code, const ir::CodeText& from) {
    std::vector<bool> taken(from.spans.size(), false);
    // substrate generic algorithm: each span claims the first unclaimed span of
    // the baseline that says the same, which writes the claim table.
    for (ir::Span& span : code.spans) {
        const auto indices = std::views::iota(std::size_t{0}, from.spans.size());
        const auto found   = std::ranges::find_if(indices, [&](std::size_t i) {
            return !taken[i] && from.spans[i].kind == span.kind && from.spans[i].payload == span.payload;
        });
        if (found == indices.end())
            continue;
        taken[*found] = true;
        if (!span.no_correction)
            span.no_correction = from.spans[*found].no_correction;
    }
}

// A declaration that pairs with a baseline declaration is typeset as the
// baseline's is (issue #330). The draft picks `\exposid` or `\exposidnc` by
// hand, and the importer keeps the choice; an extracted declaration has none,
// and took its context's default, so a paper's own wording differed from the
// draft's in a token the paper does not change. Each signature takes the forms
// of the baseline signature it reads most like. A baseline that was generated
// has no forms to give, and nothing moves.
void take_declaration_form(ir::SpecItem& item, const ir::SpecItem& from) {
    if (from.decl.signatures.empty())
        return;
    std::ranges::for_each(item.decl.signatures, [&](ir::CodeText& code) {
        const auto alike = [&](const ir::CodeText& other) {
            return code == other ? 1.0 : diff::declaration_similarity(as_imported(code), as_imported(other));
        };
        take_form(code, *std::ranges::max_element(from.decl.signatures, std::ranges::less{}, alike));
    });
}

// The declarations of a synopsis, without the namespace it is enclosed in:
// `namespace std {` and its `}` are no part of what a declaration reads like.
ir::CodeText declarations_of(const ir::CodeText& code) {
    if (!code.text.starts_with("namespace "))
        return code;
    const std::size_t open  = code.text.find("{\n");
    const std::size_t close = code.text.rfind("\n}");
    if (open == std::string::npos || close == std::string::npos || close <= open + 2)
        return code;
    const std::size_t begin = open + 2;
    ir::CodeText      out{code.text.substr(begin, close - begin), {}};
    std::ranges::for_each(code.spans, [&](ir::Span span) {
        if (span.begin < begin || span.end > close)
            return;
        span.begin -= begin;
        span.end -= begin;
        out.spans.push_back(std::move(span));
    });
    return out;
}

// What the description beside `synopsis` inherits (issue #332): the
// description of the baseline declaration that reads most like the synopsis's.
// The draft writes such a declaration as an item, or as a declaration in a
// code block, and its description as the item's, or as the paragraphs after
// the declaration, up to the next node that declares something. Each baseline
// node it takes is claimed. Nothing reading alike is no description.
std::optional<ir::SpecItem> describe_by_declaration(const ir::Synopsis&          synopsis,
                                                    const std::vector<ir::Node>& baseline,
                                                    std::vector<bool>&           claimed) {
    const ir::CodeText ours  = as_imported(declarations_of(synopsis.code));
    const auto         score = [&](std::size_t i) -> double {
        if (claimed[i])
            return 0.0;
        if (const auto* item = std::get_if<ir::SpecItem>(&baseline[i]))
            return std::ranges::fold_left(item->decl.signatures, 0.0, [&](double best, const ir::CodeText& sig) {
                return std::max(best, diff::declaration_similarity(ours, as_imported(sig)));
            });
        if (const auto* other = std::get_if<ir::Synopsis>(&baseline[i]))
            return diff::declaration_similarity(ours, as_imported(declarations_of(other->code)));
        return 0.0;
    };
    const auto indices = std::views::iota(std::size_t{0}, baseline.size());
    const auto best    = std::ranges::max_element(indices, std::ranges::less{}, score);
    if (best == indices.end() || score(*best) < kReadingThreshold)
        return std::nullopt;
    claimed[*best] = true;
    if (const auto* item = std::get_if<ir::SpecItem>(&baseline[*best]);
        item != nullptr && ir::has_description(item->descr))
        return *item;
    ir::SpecItem from;
    // substrate generic algorithm: a scan forward from the paired declaration
    // that stops at the first node declaring something, gathering the
    // paragraphs before it and claiming each.
    for (std::size_t j = *best + 1; j < baseline.size() && !claimed[j]; ++j) {
        if (const auto* prose = std::get_if<ir::FreeParagraph>(&baseline[j])) {
            from.descr.leading.push_back(ir::BodyParagraph{prose->text});
        } else if (const auto* item = std::get_if<ir::SpecItem>(&baseline[j]);
                   item != nullptr && item->decl.signatures.empty()) {
            from.descr.leading.append_range(item->descr.leading);
            from.descr.elements.append_range(item->descr.elements);
        } else {
            break;
        }
        claimed[j] = true;
    }
    return from;
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

// What cannot be patched is not reported here: `--validate` compares the
// result with the class's own declarations and says so once, with the same
// words it uses for an authored synopsis.
//
// A member the class has from a base class is not among its declarations. Its
// line takes its specifiers from the item that declares it, so the synopsis
// and that item, two declarations of one member, agree (issue #329).
void resolve_synopsis(ir::Synopsis& synopsis, const ir::Synopsis& from, const synopsis_check::Elsewhere& elsewhere) {
    synopsis.code =
        synopsis.declared ? synopsis_check::patch(from.code, *synopsis.declared, elsewhere).code : from.code;
    synopsis.as_draft = false;
}

// `enclosing` is the node lists from the document's own down to `current`,
// which is where a synopsis looks for the items that declare what its class
// does not.
void resolve_nodes(std::vector<ir::Node>&                     current,
                   const std::vector<ir::Node>&               baseline,
                   const std::string&                         where,
                   std::vector<const std::vector<ir::Node>*>& enclosing,
                   std::vector<Diagnostic>&                   out) {
    enclosing.push_back(&current);
    std::vector<bool> claimed(baseline.size(), false);
    // The items and synopses that ask to inherit, noted before the walk clears
    // the request.
    const std::vector<bool> asked =
        current | std::views::transform([](const ir::Node& node) {
            const auto* item     = std::get_if<ir::SpecItem>(&node);
            const auto* synopsis = std::get_if<ir::Synopsis>(&node);
            return (item != nullptr && item->as_draft) || (synopsis != nullptr && synopsis->as_draft);
        }) |
        std::ranges::to<std::vector>();
    // The synopsis just before the node being resolved, which a description
    // beside it pairs by (`ir::SpecItem::describes_synopsis`).
    const ir::Synopsis* beside = nullptr;
    // substrate generic algorithm: nodes of different kinds resolve differently,
    // and an item's claim on a baseline item is state shared across the siblings.
    for (ir::Node& node : current) {
        std::visit(
            overloaded{
                [&](ir::Section& section) {
                    const ir::Section* paired = find_section(baseline, section.stable_name);
                    if (paired != nullptr) {
                        resolve_nodes(section.children, paired->children, section.stable_name, enclosing, out);
                        return;
                    }
                    // A section the baseline lacks has nothing to inherit; an item in
                    // it that asks to is reported by the walk with an empty baseline.
                    resolve_nodes(section.children, {}, section.stable_name, enclosing, out);
                },
                [&](ir::SpecItem& item) {
                    if (!item.as_draft)
                        return;
                    if (item.describes_synopsis) {
                        const std::optional<ir::SpecItem> from =
                            beside == nullptr ? std::nullopt : describe_by_declaration(*beside, baseline, claimed);
                        if (!from || !ir::has_description(from->descr)) {
                            out.push_back({Severity::Error,
                                           beside == nullptr ? first_signature(item) : beside->name,
                                           std::format("\\asdraft: no declaration of the baseline{}{} that "
                                                       "reads like this one has a description",
                                                       where.empty() ? "" : "'s section ",
                                                       where)});
                            return;
                        }
                        inherit_description(item, *from, out);
                        item.describes_synopsis = false;
                        return;
                    }
                    const ir::SpecItem* from = pair_item(item, baseline, claimed);
                    if (from == nullptr) {
                        out.push_back({Severity::Error,
                                       first_signature(item),
                                       std::format("\\asdraft: no item of the baseline{}{} declares this",
                                                   where.empty() ? "" : "'s section ",
                                                   where)});
                        return;
                    }
                    take_declaration_form(item, *from);
                    inherit_description(item, *from, out);
                },
                [&](ir::Synopsis& synopsis) {
                    if (!synopsis.as_draft)
                        return;
                    const ir::Synopsis* from = pair_synopsis(synopsis, baseline, claimed);
                    if (from == nullptr) {
                        out.push_back({Severity::Error,
                                       synopsis.name + "/synopsis",
                                       std::format("\\asdraft-synopsis: no synopsis of `{}` in the baseline{}{}",
                                                   synopsis.name,
                                                   where.empty() ? "" : "'s section ",
                                                   where)});
                        return;
                    }
                    resolve_synopsis(synopsis, *from, synopsis_check::declared_elsewhere(enclosing));
                },
                [](ir::FreeParagraph&) {},
            },
            node);
        beside = std::get_if<ir::Synopsis>(&node);
    }
    // What is left of the baseline's items pairs with the items that inherit
    // nothing, for the typesetting of their declarations alone. After the walk
    // above, so that no `\asdraft` item loses its pair to one of these.
    std::ranges::for_each(std::views::zip(current, asked), [&](auto&& entry) {
        auto* item = std::get_if<ir::SpecItem>(&std::get<0>(entry));
        if (item == nullptr || std::get<1>(entry) || item->decl.signatures.empty())
            return;
        if (const ir::SpecItem* from = pair_item(*item, baseline, claimed))
            take_declaration_form(*item, *from);
    });
    // And an extracted synopsis with the baseline's synopsis of its class, for
    // the forms of its exposition-only names (issue #341): the n-th span naming
    // a member takes the form of the baseline's n-th span naming it, which is
    // the same member where the two list the members in one order. An
    // inherited synopsis is the baseline's already, and an authored one keeps
    // its escapes as typed.
    std::ranges::for_each(std::views::zip(current, asked), [&](auto&& entry) {
        auto* synopsis = std::get_if<ir::Synopsis>(&std::get<0>(entry));
        if (synopsis == nullptr || std::get<1>(entry) || synopsis->code.spans.empty())
            return;
        if (const ir::Synopsis* from = pair_synopsis(*synopsis, baseline, claimed))
            take_form(synopsis->code, from->code);
    });
    enclosing.pop_back();
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
    std::vector<Diagnostic>                   out;
    std::vector<const std::vector<ir::Node>*> enclosing;
    resolve_nodes(current.nodes, baseline.nodes, {}, enclosing, out);
    return out;
}

} // namespace beman::specgen::inherit
