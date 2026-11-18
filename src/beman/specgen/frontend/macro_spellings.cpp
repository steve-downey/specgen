// src/beman/specgen/frontend/macro_spellings.cpp                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/macro_spellings.hpp>

#include <beman/specgen/foundation/overloaded.hpp>
#include <beman/specgen/ir.hpp>

#include <clang/Basic/IdentifierTable.h>
#include <clang/Basic/TokenKinds.h>
#include <clang/Lex/MacroInfo.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Lex/Token.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace beman::specgen::frontend {

namespace {

bool is_word(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool is_blank(char c) { return c == ' ' || c == '\t'; }

// One token of an object-like macro's body: a keyword, or a name, which may be
// another macro's.
struct BodyToken {
    std::string spelling;
    bool        keyword = false;
};
using Body = std::vector<BodyToken>;

// The body of an object-like macro, when it is keywords and names alone.

std::optional<Body> keyword_body(const clang::Preprocessor& pp, const clang::MacroInfo& info) {
    if (info.isFunctionLike() || info.isBuiltinMacro())
        return std::nullopt;
    Body body;
    // substrate generic algorithm: a short-circuiting transform over the
    // macro's tokens, which fails on the first that is not a keyword or a name.
    for (const clang::Token& token : info.tokens()) {
        const bool keyword = clang::tok::getKeywordSpelling(token.getKind()) != nullptr;
        if (!keyword && !token.is(clang::tok::identifier))
            return std::nullopt;
        body.push_back({pp.getSpelling(token), keyword});
    }
    return body;
}

struct Edit {
    std::size_t begin = 0;
    std::size_t end   = 0;
    std::string text;
};

// Where the code that is not a comment or a literal continues after `i`, when
// `i` opens a comment or a literal; `i` itself otherwise.
std::size_t past_comment_or_literal(std::string_view text, std::size_t i) {
    if (text.substr(i).starts_with("//")) {
        const std::size_t end = text.find('\n', i);
        return end == std::string_view::npos ? text.size() : end;
    }
    if (text.substr(i).starts_with("/*")) {
        const std::size_t end = text.find("*/", i + 2);
        return end == std::string_view::npos ? text.size() : end + 2;
    }
    if (text[i] == '"' || text[i] == '\'') {
        const char  quote = text[i];
        std::size_t j     = i + 1;
        // substrate generic algorithm: a cursor scan to a literal's closing
        // quote, past its escapes.
        while (j < text.size() && text[j] != quote && text[j] != '\n')
            j += text[j] == '\\' ? 2 : 1;
        return std::min(j + 1, text.size());
    }
    return i;
}

std::vector<Edit> edits_of(const ir::CodeText& code, const std::map<std::string, std::string>& macros) {
    const std::string_view text = code.text;
    std::vector<Edit>      out;
    std::size_t            i = 0;
    // substrate generic algorithm: a lexer's scan, whose step is a comment, a
    // literal, a word or one character; the position is its state.
    while (i < text.size()) {
        if (const std::size_t past = past_comment_or_literal(text, i); past != i) {
            i = past;
            continue;
        }
        if (!is_word(text[i]) || (i > 0 && is_word(text[i - 1]))) {
            ++i;
            continue;
        }
        std::size_t end = i;
        // substrate generic algorithm: a cursor scan to the end of a word.
        while (end < text.size() && is_word(text[end]))
            ++end;
        const auto found = macros.find(std::string(text.substr(i, end - i)));
        const bool spanned =
            std::ranges::any_of(code.spans, [&](const ir::Span& s) { return s.begin < end && i < s.end; });
        if (found != macros.end() && !spanned) {
            Edit edit{i, end, found->second};
            if (edit.text.empty()) {
                std::size_t after = end;
                // substrate generic algorithm: a cursor scan over a run of blanks.
                while (after < text.size() && is_blank(text[after]))
                    ++after;
                if (after > end) {
                    edit.end = after;
                } else {
                    // substrate generic algorithm: a cursor scan back over a run of blanks.
                    while (edit.begin > 0 && is_blank(text[edit.begin - 1]))
                        --edit.begin;
                }
            }
            out.push_back(std::move(edit));
        }
        i = end;
    }
    return out;
}

// Where a position of the text before `edit` is in the text after it.
std::size_t moved(std::size_t at, const Edit& edit) {
    if (at <= edit.begin)
        return at;
    if (at < edit.end)
        return edit.begin;
    return at - (edit.end - edit.begin) + edit.text.size();
}

} // namespace

std::map<std::string, std::string> specifier_macros(const clang::Preprocessor& pp) {
    std::map<std::string, Body> bodies;
    // substrate generic algorithm: a filter-map over the preprocessor's macro
    // table into a map keyed by name.
    for (const auto& [identifier, state] : pp.macros(false)) {
        const clang::MacroInfo* info = pp.getMacroInfo(identifier);
        if (info == nullptr)
            continue;
        if (std::optional<Body> body = keyword_body(pp, *info))
            bodies.emplace(identifier->getName().str(), std::move(*body));
    }
    // A name in a body is another macro's; it stands for that one's
    // expansion, if that is keywords too. A cycle, or a name that is no such
    // macro, leaves the macro out.
    std::map<std::string, std::string> out;
    const auto expand = [&](this const auto& self, const std::string& name, int depth) -> std::optional<std::string> {
        const auto found = bodies.find(name);
        if (found == bodies.end() || depth > 16)
            return std::nullopt;
        std::string spelled;
        // substrate generic algorithm: a short-circuiting join over the body.
        for (const BodyToken& token : found->second) {
            std::string piece = token.spelling;
            if (!token.keyword) {
                std::optional<std::string> inner = self(token.spelling, depth + 1);
                if (!inner)
                    return std::nullopt;
                piece = std::move(*inner);
            }
            if (!piece.empty())
                spelled += (spelled.empty() ? "" : " ") + piece;
        }
        return spelled;
    };
    std::ranges::for_each(bodies, [&](const auto& entry) {
        if (std::optional<std::string> spelled = expand(entry.first, 0))
            out.emplace(entry.first, std::move(*spelled));
    });
    return out;
}

ir::CodeText expand_specifier_macros(ir::CodeText code, const std::map<std::string, std::string>& macros) {
    if (macros.empty())
        return code;
    const std::vector<Edit> edits = edits_of(code, macros);
    // Last to first, so each edit's positions are those of the text it edits.
    std::ranges::for_each(edits | std::views::reverse, [&](const Edit& edit) {
        code.text.replace(edit.begin, edit.end - edit.begin, edit.text);
        std::ranges::for_each(code.spans, [&](ir::Span& span) {
            span.begin = moved(span.begin, edit);
            span.end   = moved(span.end, edit);
        });
        std::ranges::for_each(code.hints, [&](ir::LayoutHint& hint) { hint.at = moved(hint.at, edit); });
    });
    return code;
}

void expand_specifier_macros(ir::Document& document, const std::map<std::string, std::string>& macros) {
    if (macros.empty())
        return;
    const auto expand = [&](ir::CodeText& code) { code = expand_specifier_macros(std::move(code), macros); };
    const auto walk   = [&](this const auto& self, std::vector<ir::Node>& nodes) -> void {
        std::ranges::for_each(nodes, [&](ir::Node& node) {
            std::visit(foundation::overloaded{
                           [&](ir::Section& section) { self(section.children); },
                           [&](ir::Synopsis& synopsis) {
                               if (synopsis.declared)
                                   expand(*synopsis.declared);
                               else
                                   expand(synopsis.code);
                               std::ranges::for_each(synopsis.declared_bases, expand);
                           },
                           [&](ir::SpecItem& item) { std::ranges::for_each(item.decl.signatures, expand); },
                           [](ir::FreeParagraph&) {},
                       },
                       node);
        });
    };
    walk(document.nodes);
}

} // namespace beman::specgen::frontend
