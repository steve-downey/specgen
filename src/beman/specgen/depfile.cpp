// src/beman/specgen/depfile.cpp                                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/depfile.hpp>

#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace beman::specgen::depfile {

namespace {

// One path, escaped and prefixed with the continuation that puts it on its own
// line: the prerequisite list is one logical line however long it gets, and a
// header per line is what makes the fragment readable when a build is being
// debugged by reading it.
std::string continued(const std::string& path) { return std::format(" \\\n  {}", escape(path)); }

// One prerequisite's `-MP` rule.
std::string phony_rule(const std::string& path) { return std::format("{}:\n", escape(path)); }

// Concatenate a range of strings. The pieces are already whole lines or whole
// fragments of one; joining them is the last step in every branch below.
std::string concat(auto&& pieces) {
    return std::forward<decltype(pieces)>(pieces) | std::views::join | std::ranges::to<std::string>();
}

} // namespace

std::string escape(std::string_view path) {
    return concat(path | std::views::transform([](char c) -> std::string {
                      switch (c) {
                      case ' ':
                          return "\\ ";
                      case '#':
                          return "\\#";
                      case '$':
                          return "$$";
                      default:
                          return std::string(1, c);
                      }
                  }));
}

std::string format(const std::vector<std::string>& targets, const std::vector<std::string>& prerequisites) {
    if (targets.empty())
        return {};

    const std::string target_list =
        targets | std::views::transform(escape) | std::views::join_with(' ') | std::ranges::to<std::string>();
    const std::string prerequisite_list = concat(prerequisites | std::views::transform(continued));

    // No prerequisites means no `-MP` block to write, and the rule stands
    // alone: a target that depends on nothing is still a target make can be
    // told about.
    if (prerequisites.empty())
        return std::format("{}:\n", target_list);

    return std::format(
        "{}:{}\n\n{}", target_list, prerequisite_list, concat(prerequisites | std::views::transform(phony_rule)));
}

} // namespace beman::specgen::depfile
