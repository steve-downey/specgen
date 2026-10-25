// src/beman/specgen/section_markers.cpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/section_markers.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace beman::specgen::grammar {

std::optional<SectionEndFence> section_end_fence(std::string_view raw) {
    std::size_t begin = 0;
    // substrate generic algorithm: preserve byte offsets while scanning a
    // possibly merged RawComment for the exact physical fence line.
    while (begin <= raw.size()) {
        const std::size_t newline = raw.find('\n', begin);
        const std::size_t end     = newline == std::string_view::npos ? raw.size() : newline;
        std::string_view  text    = raw.substr(begin, end - begin);
        const std::size_t first   = text.find_first_not_of(" \t");
        if (first != std::string_view::npos) {
            text.remove_prefix(first);
            if (text.starts_with("///")) {
                text.remove_prefix(3);
                const std::size_t body = text.find_first_not_of(" \t");
                if (body != std::string_view::npos)
                    text.remove_prefix(body);
                const std::size_t last = text.find_last_not_of(" \t\r");
                text = last == std::string_view::npos ? std::string_view{} : text.substr(0, last + 1);
                constexpr std::string_view prefix = "END [";
                if (text.starts_with(prefix) && text.ends_with(']')) {
                    const std::string_view stable = text.substr(prefix.size(), text.size() - prefix.size() - 1);
                    if (!stable.empty())
                        return SectionEndFence{std::string(stable), begin};
                }
            }
        }
        if (newline == std::string_view::npos)
            break;
        begin = newline + 1;
    }
    return std::nullopt;
}

std::optional<std::string> parse_ref(std::string_view raw) {
    std::size_t pos     = 0;
    auto        skip_ws = [&] {
        pos = raw.find_first_not_of(" \t", pos);
        if (pos == std::string_view::npos)
            pos = raw.size();
    };

    skip_ws();
    if (raw.compare(pos, 3, "///") == 0 || raw.compare(pos, 3, "//!") == 0)
        pos += 3;
    else if (raw.compare(pos, 2, "//") == 0)
        pos += 2;
    else
        return std::nullopt;
    skip_ws();

    static constexpr std::string_view kTag = "\\ref";
    if (raw.compare(pos, kTag.size(), kTag) != 0)
        return std::nullopt;
    pos += kTag.size();

    skip_ws();
    if (pos >= raw.size() || raw[pos] != '{')
        return std::nullopt;
    ++pos;
    const std::size_t stable_begin = pos;
    const std::size_t stable_end   = raw.find('}', pos);
    if (stable_end == std::string_view::npos)
        return std::nullopt;
    return std::string(raw.substr(stable_begin, stable_end - stable_begin));
}

} // namespace beman::specgen::grammar
