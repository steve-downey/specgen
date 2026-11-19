// include/beman/specgen/backend/indent_lines.hpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen -- a rendered block moved under a list item, for the backends whose
// lists nest by indentation.
#ifndef BEMAN_SPECGEN_BACKEND_INDENT_LINES_HPP
#define BEMAN_SPECGEN_BACKEND_INDENT_LINES_HPP

#include <ranges>
#include <string>
#include <string_view>

namespace beman::specgen::backend {

// Each line of `text` with `indent` before it, but an empty line left empty,
// so a blank line between two blocks stays blank and no line ends in
// whitespace. Markdown and org put a block inside a list item this way
// (issue #367).
inline std::string indent_lines(std::string_view text, std::string_view indent) {
    return text | std::views::split('\n') | std::views::transform([&](auto&& line) {
               const std::string_view view(line.begin(), line.end());
               return view.empty() ? std::string{} : std::string(indent) + std::string(view);
           }) |
           std::views::join_with('\n') | std::ranges::to<std::string>();
}

} // namespace beman::specgen::backend

#endif // BEMAN_SPECGEN_BACKEND_INDENT_LINES_HPP
