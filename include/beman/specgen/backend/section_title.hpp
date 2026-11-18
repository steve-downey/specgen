// include/beman/specgen/backend/section_title.hpp                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_BACKEND_SECTION_TITLE_HPP
#define BEMAN_SPECGEN_BACKEND_SECTION_TITLE_HPP

#include <beman/specgen/backend/draft_spans.hpp>
#include <beman/specgen/parse/cursor.hpp>
#include <beman/specgen/parse/parser.hpp>

#include <cstddef>
#include <format>
#include <string>
#include <string_view>

namespace beman::specgen::backend {

namespace section_title_detail {

// `\&`, `\%`, `\#`, `\_`, `\{`, `\}` and `\$` stand for the character in
// LaTeX's text mode; anything else after a backslash is a command.
inline bool is_escaped_character(std::string_view rest) {
    return rest.size() > 1 && rest[0] == '\\' && std::string_view("&%#_{}$").contains(rest[1]);
}

// The text of a title outside code, for a backend that does not write LaTeX:
// an escaped character is the character (issue #351).
inline std::string unescaped(std::string_view text) {
    std::string out;
    // substrate generic algorithm: a cursor scan that drops the backslash of
    // an escaped character.
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (is_escaped_character(text.substr(i)))
            ++i;
        out.push_back(text[i]);
    }
    return out;
}

} // namespace section_title_detail

// Titles retain authored markup in the IR: the draft's own, `\tcode{...}`, from
// the importer and from a header that writes it, or a docblock's backticks.
// Translate the code markup for non-draft headings without interpreting
// unknown TeX commands; inside the code and out of it, an escaped character is
// the character.
inline std::string render_section_title(std::string_view title, char code_delimiter) {
    std::string                out;
    std::size_t                pos   = 0;
    constexpr std::string_view tcode = "\\tcode";
    // substrate generic algorithm: a markup scan with variable-width braced arguments.
    while (pos < title.size()) {
        if (title.substr(pos).starts_with(tcode)) {
            const auto argument = parse::braced()(parse::cursor{title.substr(pos + tcode.size())});
            if (argument) {
                out += std::format(
                    "{}{}{}", code_delimiter, section_title_detail::unescaped(argument->value), code_delimiter);
                pos += tcode.size() + argument->rest.position().offset;
                continue;
            }
        }
        if (section_title_detail::is_escaped_character(title.substr(pos))) {
            out.push_back(title[pos + 1]);
            pos += 2;
            continue;
        }
        out.push_back(title[pos] == '`' ? code_delimiter : title[pos]);
        ++pos;
    }
    return out;
}

// The same title as the draft's LaTeX (issue #351). A backticked span is
// `\tcode{...}`, its text escaped as a code inline's is in prose; outside code,
// `&`, `%` and `#` are escaped as prose's are (`draft_escape_prose_text`). The
// draft's own markup passes through: a command and its argument are the
// author's LaTeX, and an escaped character is escaped already.
inline std::string render_section_title_draft(std::string_view title) {
    std::string out;
    std::size_t pos = 0;
    // substrate generic algorithm: a markup scan whose step is a code span, a
    // control sequence's first two characters, or one character.
    while (pos < title.size()) {
        if (title[pos] == '`') {
            const std::size_t close = title.find('`', pos + 1);
            if (close != std::string_view::npos) {
                out += "\\tcode{" + draft_escape_code_text(title.substr(pos + 1, close - pos - 1)) + '}';
                pos = close + 1;
                continue;
            }
        }
        if (title[pos] == '\\' && pos + 1 < title.size()) {
            out += title.substr(pos, 2);
            pos += 2;
            continue;
        }
        out += draft_escape_prose_text(title.substr(pos, 1));
        ++pos;
    }
    return out;
}

} // namespace beman::specgen::backend

#endif // BEMAN_SPECGEN_BACKEND_SECTION_TITLE_HPP
