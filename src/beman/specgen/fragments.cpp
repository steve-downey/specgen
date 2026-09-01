// src/beman/specgen/fragments.cpp                                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The split is a decomposition, not a rendering: nothing here knows what a
// fragment will be written as, and the only text it produces is a name.
// See the header for what design §8 asks of it.

#include <beman/specgen/fragments.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace beman::specgen::fragments {

namespace {

// A stable name as its dotted components. `optional.ctor` is
// {"optional", "ctor"}, which is the granularity a common prefix has to be
// measured at: `optional.ctor` and `optional.compare` share the text
// `optional.c`, and share no component past `optional`.
std::vector<std::string> components(std::string_view name) {
    return name | std::views::split('.') |
           std::views::transform([](auto part) { return std::string(part.begin(), part.end()); }) |
           std::ranges::to<std::vector>();
}

std::string join(const std::vector<std::string>& parts) {
    return parts | std::views::join_with('.') | std::ranges::to<std::string>();
}

// The longest component sequence every one of `all` starts with.
std::vector<std::string> common_prefix(const std::vector<std::vector<std::string>>& all) {
    if (all.empty())
        return {};
    return std::ranges::fold_left(all | std::views::drop(1),
                                  all.front(),
                                  [](std::vector<std::string> acc, const std::vector<std::string>& next) {
                                      acc.erase(std::ranges::mismatch(acc, next).in1, acc.end());
                                      return acc;
                                  });
}

// The name for the fragment holding whatever sits outside every section,
// derived from the sections themselves: five `optional.*` sections name it
// `optional`.
//
// The shortening step is what makes a one-section document work. The common
// prefix of a single name is that whole name, so `stack.access` alone would
// name the root fragment `stack.access` — the path its own section already
// claims. Dropping the last component says the same thing the multi-section
// case says: the root fragment is the parent of the sections it precedes.
std::optional<std::string> derive_root(const std::vector<std::string>& section_names) {
    std::vector<std::string> parts =
        common_prefix(section_names | std::views::transform([](const std::string& name) { return components(name); }) |
                      std::ranges::to<std::vector>());
    if (!parts.empty() && std::ranges::contains(section_names, join(parts)))
        parts.pop_back();
    if (parts.empty())
        return std::nullopt;
    return join(parts);
}

// The accumulator of the placement fold below: the fragments so far, plus
// where the root fragment landed once a loose node has opened one.
struct Placement {
    std::vector<Fragment>      fragments;
    std::optional<std::size_t> root;
};

} // namespace

bool usable_as_file_name(std::string_view name) {
    if (name.empty() || name.front() == '.' || name.front() == '-' || name.back() == '.')
        return false;
    if (name.contains(".."))
        return false;
    return std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
               c == '-' || c == '+';
    });
}

std::expected<std::vector<Fragment>, Error> split(const ir::Document& document, const Options& options) {
    // A section with no stable name is reported before anything else, because
    // the root name is derived *from* the section names and a nameless one
    // would silently widen the prefix it is measured against.
    //
    // `sections` is not `const`: a filter_view caches its first element and so
    // is never const-iterable, which is a property of the view rather than of
    // anything done with it here.
    auto sections =
        document.nodes |
        std::views::filter([](const ir::Node& node) { return std::holds_alternative<ir::Section>(node); }) |
        std::views::transform([](const ir::Node& node) -> const ir::Section& { return std::get<ir::Section>(node); });
    if (const auto unnamed =
            std::ranges::find_if(sections, [](const ir::Section& section) { return section.stable_name.empty(); });
        unnamed != std::ranges::end(sections)) {
        const std::string& title = (*unnamed).title;
        const std::string  which =
            title.empty() ? std::string{"an untitled section"} : std::format("the section '{}'", title);
        return std::unexpected(Error{std::format("{} has no stable name to derive a fragment path from", which)});
    }

    // The split itself: one fragment per top-level section, every other
    // top-level node into the root fragment, which is created — empty and
    // still unnamed — at the position of the first such node, so the
    // fragments come out in document order rather than sections-then-rest.
    Placement placed = std::ranges::fold_left(document.nodes, Placement{}, [](Placement acc, const ir::Node& node) {
        if (const auto* section = std::get_if<ir::Section>(&node)) {
            Fragment fragment;
            fragment.name = section->stable_name;
            fragment.document.nodes.push_back(node);
            acc.fragments.push_back(std::move(fragment));
            return acc;
        }
        if (!acc.root) {
            acc.root = acc.fragments.size();
            acc.fragments.emplace_back();
        }
        acc.fragments[*acc.root].document.nodes.push_back(node);
        return acc;
    });

    if (placed.root) {
        const std::size_t loose = placed.fragments[*placed.root].document.nodes.size();
        std::string       name  = options.root;
        if (name.empty()) {
            const std::optional<std::string> derived = derive_root(
                sections | std::views::transform(&ir::Section::stable_name) | std::ranges::to<std::vector>());
            if (!derived) {
                const std::string count =
                    loose == 1 ? std::string{"one top-level node sits"} : std::format("{} top-level nodes sit", loose);
                return std::unexpected(
                    Error{std::format("{} outside every section, and the section stable names share no common prefix "
                                      "to name it by",
                                      count),
                          true});
            }
            name = *derived;
        }
        placed.fragments[*placed.root].name = std::move(name);
    }

    if (const auto bad = std::ranges::find_if(
            placed.fragments, [](const Fragment& fragment) { return !usable_as_file_name(fragment.name); });
        bad != placed.fragments.end())
        return std::unexpected(Error{std::format("'{}' cannot name a file, so it cannot name a fragment", bad->name)});

    // Two `\rSec3`s writing one stable name, or a root name colliding with a
    // section's. Either way the second write would overwrite the first, and a
    // silently short output set is the one failure a wholesale regeneration
    // scheme must not have.
    std::vector<std::string_view> names =
        placed.fragments | std::views::transform(&Fragment::name) | std::ranges::to<std::vector<std::string_view>>();
    std::ranges::sort(names);
    if (const auto duplicate = std::ranges::adjacent_find(names); duplicate != names.end())
        return std::unexpected(Error{std::format("two fragments would both be named '{}'", *duplicate)});

    return std::move(placed.fragments);
}

} // namespace beman::specgen::fragments
