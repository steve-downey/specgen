// src/beman/specgen/document_build.cpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/document_build.hpp>

#include <beman/specgen/foundation/overloaded.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <map>
#include <ranges>
#include <utility>

namespace beman::specgen::document_build {

namespace ir = beman::specgen::ir;

void append_grouped_itemdecl(ir::ItemDecl& primary, ir::ItemDecl&& follower) {
    primary.signatures.append_range(std::move(follower.signatures));

    std::vector<ir::IndexEntry> indexes;
    indexes.reserve(primary.index.size() + follower.index.size());
    const auto append_distinct = [&](ir::IndexEntry& entry) {
        const auto same_entry = [&](const ir::IndexEntry& existing) {
            return existing.kind == entry.kind && existing.name == entry.name && existing.parent == entry.parent;
        };
        if (std::ranges::find_if(indexes, same_entry) == indexes.end())
            indexes.push_back(std::move(entry));
    };
    std::ranges::for_each(primary.index, append_distinct);
    std::ranges::for_each(follower.index, append_distinct);
    primary.index = std::move(indexes);
}

namespace {

// The one destructive operation shared by adjacent and named grouping. The
// primary remains in place with its description and placement key; the
// follower contributes signatures and any exact-distinct index metadata.
void append_signatures(GroupCandidate& primary, GroupCandidate& follower) {
    auto& primary_item  = std::get<beman::specgen::ir::SpecItem>(primary.node);
    auto& follower_item = std::get<beman::specgen::ir::SpecItem>(follower.node);
    append_grouped_itemdecl(primary_item.decl, std::move(follower_item.decl));
}

} // namespace

std::vector<GroupCandidate> group_items(std::vector<GroupCandidate> candidates, std::vector<Diagnostic>* diagnostics) {
    std::vector<GroupCandidate>        grouped;
    std::map<std::string, std::size_t> named_primaries;
    std::optional<std::size_t>         adjacent_primary;
    grouped.reserve(candidates.size());

    const auto report = [&](unsigned line, std::string message) {
        if (diagnostics != nullptr)
            diagnostics->push_back(Diagnostic{Severity::Error, line, std::move(message)});
    };

    // substrate generic algorithm: this is a left-to-right stateful fold over
    // push order. Each step can register a primary, resolve a prior primary,
    // merge into one of several earlier output positions, or retain the
    // candidate while updating adjacency. A transform/fold expression would
    // conceal those non-local mutations rather than state the operation.
    for (GroupCandidate& candidate : candidates) {
        // A *signature-carrying* item, which is what grouping moves: grouping
        // appends a follower's signatures to a primary's itemdecl, so an item
        // with no itemdecl at all -- a class's own description (issue #18) --
        // is neither. Treating it as one would relabel that description as
        // the wording of whichever `\also` member happened to follow it.
        const bool is_item = std::holds_alternative<ir::SpecItem>(candidate.node) &&
                             !std::get<ir::SpecItem>(candidate.node).decl.signatures.empty();

        if (candidate.group_id && candidate.also_target) {
            grouped.push_back(std::move(candidate));
            adjacent_primary = is_item ? std::optional{grouped.size() - 1} : std::nullopt;
            continue;
        }

        if (candidate.also_target) {
            const auto primary = named_primaries.find(*candidate.also_target);
            if (is_item && primary != named_primaries.end()) {
                append_signatures(grouped[primary->second], candidate);
                adjacent_primary = primary->second;
                continue;
            }
            report(candidate.grouping_line,
                   std::format("\\also target '{}' has no preceding \\group primary in this section",
                               *candidate.also_target));
        } else if (candidate.wants_join && is_item && adjacent_primary &&
                   std::holds_alternative<ir::SpecItem>(grouped[*adjacent_primary].node)) {
            append_signatures(grouped[*adjacent_primary], candidate);
            continue;
        }

        grouped.push_back(std::move(candidate));
        const std::size_t position = grouped.size() - 1;
        adjacent_primary           = is_item ? std::optional{position} : std::nullopt;
        if (grouped.back().group_id && is_item) {
            const auto [_, inserted] = named_primaries.emplace(*grouped.back().group_id, position);
            if (!inserted)
                report(grouped.back().grouping_line,
                       std::format("duplicate \\group id '{}' in this section", *grouped.back().group_id));
        }
    }
    return grouped;
}

namespace {

// A section under construction (decision document-build-stages, stage 2). Frames are
// kept on an explicit stack rather than as pointers into ir::Section::
// children, because the children vectors this builds are mutated (and may
// reallocate) while nested frames are still open — a pointer into an
// ancestor's vector would dangle the moment a sibling push reallocates it.
//
// `pushed` holds this frame's children exactly as build_tree's loop appends
// them (push order) until the frame closes: key and join-candidacy travel
// with each node in one GroupCandidate, so grouping (which needs push-order
// adjacency) and the placement-key sort (which needs the key) each read
// from the same list at the point they run, rather than from two parallel
// arrays a caller must keep in lockstep.
struct Frame {
    int                         depth = 0; // 0 for the synthetic root frame, never popped
    std::string                 stable;    // unused for the root frame
    std::string                 title;     // unused for the root frame
    std::vector<GroupCandidate> pushed;
    unsigned                    open_offset = 0; // source offset of this frame's \rSec, its key in the parent
};

// Reorder a frame's already-grouped children into placement-key order
// (design §3.3). Stable, so equal keys keep push order.
void sort_frame(Frame& f) {
    std::ranges::stable_sort(f.pushed, [](const GroupCandidate& a, const GroupCandidate& b) { return a.key < b.key; });
}

} // namespace

BuildResult build_tree(std::span<DocEvent> events) {
    namespace ir = beman::specgen::ir;

    std::vector<Frame> stack;
    stack.push_back(Frame{});

    // Diagnostics collected from the events along the way — a captured
    // accumulator the event loop below folds into, same shape as
    // `stack`/`pending` beside it.
    std::vector<Diagnostic> diagnostics;

    // In-class-defined members and hidden friends awaiting placement,
    // keyed by the stable name of the `\rSec` section their `\ref` group
    // names. Populated when a SynopsisDecl event carries pending items,
    // drained when the matching section closes.
    std::map<std::string, std::vector<PendingItem>> pending;

    // Pop the innermost open frame, group its own children in the push order
    // they were accumulated in, inject the pending items routed to its
    // section (ungrouped — see PendingItem's doc comment), sort into
    // class-body order, and fold it into its parent as an ir::Section keyed
    // by where the section opened.
    const auto close_top = [&] {
        Frame closed = std::move(stack.back());
        stack.pop_back();

        // Stage 3, run here rather than as a later pass over the sorted
        // tree — see document_build.hpp's top-of-file note. `closed.pushed`
        // is still in push order at this point, strictly before the
        // pending-member injection and placement-key sort below, neither of
        // which grouping may ever see.
        closed.pushed = group_items(std::move(closed.pushed), &diagnostics);

        if (auto it = pending.find(closed.stable); it != pending.end()) {
            // Pending items join the frame ungrouped (`wants_join` false is
            // load-bearing — see PendingItem's doc comment), each becoming
            // its own GroupCandidate keyed by its own offset.
            closed.pushed.append_range(it->second | std::views::as_rvalue | std::views::transform([](PendingItem&& p) {
                                           return GroupCandidate{p.offset, std::move(p.item), false};
                                       }));
            pending.erase(it);
        }
        sort_frame(closed);

        std::vector<ir::Node> children = closed.pushed | std::views::as_rvalue |
                                         std::views::transform([](GroupCandidate&& c) { return std::move(c.node); }) |
                                         std::ranges::to<std::vector>();

        const unsigned open_offset = closed.open_offset;
        stack.back().pushed.push_back(GroupCandidate{
            open_offset, ir::Section{std::move(closed.stable), std::move(closed.title), std::move(children)}, false});
    };

    // substrate generic algorithm (decision document-build-stages): "parse a flat sequence into a tree
    // by depth" is stated directly as a fold carrying a frame stack rather
    // than forced into unfold_with — the events arrive linearly with
    // explicit depths, so an unfold would need lookahead plumbing for no
    // gain. It mutates `stack`, `pending`, and `diagnostics` together, and
    // its effect on `stack` depends on every prior event, so this is a fold
    // with a three-part accumulator and no useful return.
    for (DocEvent& ev : events) {
        std::visit(beman::specgen::foundation::overloaded{
                       [&](SectionOpen& open) {
                           // Close every open frame this section is not nested
                           // inside, then open its own. The root frame (depth
                           // 0) is never closed here since every \rSec depth
                           // is >= 1.
                           // substrate generic algorithm: a loop-until-a-
                           // stack-condition, not a sequence walk — nothing
                           // is being traversed, only `stack`'s own state is
                           // read and `close_top` mutates it in place.
                           while (stack.back().depth >= open.depth)
                               close_top();
                           Frame opening;
                           opening.depth       = open.depth;
                           opening.stable      = std::move(open.stable);
                           opening.title       = std::move(open.title);
                           opening.open_offset = open.offset;
                           stack.push_back(std::move(opening));
                       },
                       [&](SynopsisDecl& syn) {
                           // The class's own members' docblock findings,
                           // collected before the members themselves
                           // are scattered below, since a member routed
                           // nowhere is dropped and its findings must not be.
                           diagnostics.append_range(std::move(syn.diagnostics));
                           stack.back().pushed.push_back(GroupCandidate{syn.offset, std::move(syn.synopsis), false});
                           // Same placement key, pushed second: sort_frame is
                           // stable, so the class-general paragraph remains
                           // immediately after its synopsis in the active
                           // subclause frame (design §5.2).
                           if (syn.general)
                               stack.back().pushed.push_back(
                                   GroupCandidate{syn.offset, std::move(*syn.general), false});
                           // Same placement key again, pushed last: the
                           // class's own authored description (issue #18)
                           // follows both the synopsis and the derived
                           // general paragraph it may have replaced. A
                           // description-only ir::SpecItem -- no signatures,
                           // so group_items neither joins it to anything nor
                           // lets a following \also item join onto it.
                           if (!syn.descr.elements.empty())
                               stack.back().pushed.push_back(
                                   GroupCandidate{syn.offset, ir::SpecItem{{}, std::move(syn.descr)}, false});
                           // substrate generic algorithm: distributes each
                           // in-class member to the pending bucket for its
                           // own \rSec target — a scatter keyed by data
                           // (PendingItem::stable), not by position, so no
                           // ranges algorithm (chunk_by groups by adjacency,
                           // not by an arbitrary key) names it.
                           for (PendingItem& p : syn.pending)
                               pending[p.stable].push_back(std::move(p));
                       },
                       [&](ItemDecl& item) {
                           // This item's own docblock findings,
                           // collected whether or not the grouping below
                           // folds its content into a primary.
                           diagnostics.append_range(std::move(item.diagnostics));
                           stack.back().pushed.push_back(GroupCandidate{item.placement_key,
                                                                        std::move(item.item),
                                                                        item.wants_join,
                                                                        std::move(item.group_id),
                                                                        std::move(item.also_target),
                                                                        item.grouping_line});
                       },
                       [&](Ignored& ig) {
                           // \ref group headers, license/SPDX text, trailing
                           // braces, a malformed \rSec, a non-function
                           // top-level decl, or an \omit/\merge item: none of
                           // these are structure. The last two of those are
                           // the cases classify() attaches Diagnostics to (a
                           // malformed \rSec; an omitted
                           // item's docblock findings); collect them,
                           // but the tree itself stays exactly as unaffected
                           // as for any other Ignored event.
                           diagnostics.append_range(std::move(ig.diagnostics));
                       },
                   },
                   ev);
    }

    // Flush whatever sections are still open at EOF.
    // substrate generic algorithm: the same loop-until-a-stack-condition
    // shape as the SectionOpen case above — no sequence to traverse, only
    // `stack.size()` read and `close_top` mutating it in place.
    while (stack.size() > 1)
        close_top();

    // The root frame is never closed via close_top (it is never popped), so
    // its own grouping and ordering happen here instead — the same live
    // join check applies to top-level items too, and the top-level nodes
    // are ordered at the very end (a no-op when they are already in
    // source order, which they are absent injected in-class items at the
    // root — the root is never a pending-injection target, since it never
    // goes through close_top).
    stack.front().pushed = group_items(std::move(stack.front().pushed), &diagnostics);
    sort_frame(stack.front());

    ir::Document doc{stack.front().pushed | std::views::as_rvalue |
                     std::views::transform([](GroupCandidate&& c) { return std::move(c.node); }) |
                     std::ranges::to<std::vector>()};
    return BuildResult{std::move(doc), std::move(diagnostics)};
}

} // namespace beman::specgen::document_build
