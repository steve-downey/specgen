// include/beman/specgen/backend/section_title.hpp                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_BACKEND_SECTION_TITLE_HPP
#define BEMAN_SPECGEN_BACKEND_SECTION_TITLE_HPP

#include <beman/specgen/parse/cursor.hpp>
#include <beman/specgen/parse/parser.hpp>

#include <cstddef>
#include <format>
#include <string>
#include <string_view>

namespace beman::specgen::backend {

inline std::string render_section_title(std::string_view title, char code_delimiter);

// Titles retain authored draft markup in the IR. Translate the supported code
// markup for non-draft headings without interpreting unknown TeX commands.
inline std::string render_section_title(std::string_view title, char code_delimiter) {
    std::string                out;
    std::size_t                pos   = 0;
    constexpr std::string_view tcode = "\\tcode";
    // substrate generic algorithm: a markup scan with variable-width braced arguments.
    while (pos < title.size()) {
        if (title.substr(pos).starts_with(tcode)) {
            const auto argument = parse::braced()(parse::cursor{title.substr(pos + tcode.size())});
            if (argument) {
                out += std::format("{}{}{}", code_delimiter, argument->value, code_delimiter);
                pos += tcode.size() + argument->rest.position().offset;
                continue;
            }
        }
        out.push_back(title[pos] == '`' ? code_delimiter : title[pos]);
        ++pos;
    }
    return out;
}

} // namespace beman::specgen::backend

#endif // BEMAN_SPECGEN_BACKEND_SECTION_TITLE_HPP
