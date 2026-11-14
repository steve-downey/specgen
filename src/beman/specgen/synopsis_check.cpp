// src/beman/specgen/synopsis_check.cpp                            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/synopsis_check.hpp>

#include <beman/specgen/diff.hpp>
#include <beman/specgen/ir.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <format>
#include <functional>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beman::specgen::synopsis_check {

namespace {

bool is_word(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

// One member function declaration of a synopsis, with where its parts are.
struct Decl {
    std::string name;
    std::size_t begin = 0; // the statement's first character
    std::size_t end   = 0; // its `;`
    std::size_t head_end = 0;   // after a leading `template<...>`
    std::size_t name_begin = 0;
    std::size_t params_end = 0; // just past the parameter list's `)`
    std::string text;           // begin..end, whitespace folded

    // The words of the decl-specifier region, with where each starts and how far it reaches.
    struct Word {
        std::string word;
        std::size_t begin = 0;
        std::size_t end   = 0;
    };
    std::vector<Word> prefix;
    bool              is_constexpr = false;
    bool              is_explicit  = false;
    bool              is_static    = false;
    bool              is_friend    = false;
    bool              is_noexcept  = false;
    // A placeholder in the `noexcept` operand (the draft's `noexcept(noex)`):
    // the declaration stands for both, so it agrees with either and is not patched.
    bool              noexcept_any = false;
    bool              is_default   = false;
    bool              is_delete    = false;
    std::size_t       noexcept_begin = 0; // `noexcept` and its parenthesised operand, when there is one
    std::size_t       noexcept_end   = 0;
    std::size_t       tail_insert    = 0; // where a `noexcept` would go
};

// The text with comments and access labels blanked, every offset unchanged.
std::string blanked(std::string_view text) {
    std::string out(text);
    // substrate generic algorithm: a scan over the characters that rewrites the
    // copy in place; the position is the loop's state.
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (out[i] == '/' && i + 1 < out.size() && out[i + 1] == '/') {
            // substrate generic algorithm: a cursor scan over the text; the position is the loop's state.
            while (i < out.size() && out[i] != '\n')
                out[i++] = ' ';
        }
    }
    // substrate generic algorithm: a cursor scan over the text; the position is the loop's state.
    for (const std::string_view label : {"public:", "private:", "protected:"}) {
        std::size_t at = 0;
        // substrate generic algorithm: a search resumed after each hit.
        while ((at = out.find(label, at)) != std::string::npos) {
            std::ranges::fill_n(out.begin() + static_cast<std::ptrdiff_t>(at), label.size(), ' ');
            at += label.size();
        }
    }
    return out;
}

std::size_t skip_space(std::string_view s, std::size_t i) {
    // substrate generic algorithm: a cursor scan over the text; the position is the loop's state.
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])) != 0)
        ++i;
    return i;
}

// The offset just past the bracket that closes the one at `open`, or npos.
std::size_t past_close(std::string_view s, std::size_t open, char left, char right) {
    int depth = 0;
    // substrate generic algorithm: a balanced-bracket scan; the depth is the loop's state.
    for (std::size_t i = open; i < s.size(); ++i) {
        if (s[i] == left)
            ++depth;
        else if (s[i] == right && --depth == 0)
            return i + 1;
    }
    return std::string_view::npos;
}

bool starts_with_word(std::string_view s, std::size_t at, std::string_view word) {
    return s.substr(at, word.size()) == word && (at + word.size() >= s.size() || !is_word(s[at + word.size()]));
}

std::string folded(std::string_view s) {
    std::string out;
    bool        space = false;
    // substrate generic algorithm: folds runs of whitespace to one space.
    for (const char c : s) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            space = !out.empty();
            continue;
        }
        if (space)
            out.push_back(' ');
        space = false;
        out.push_back(c);
    }
    return out;
}

std::optional<Decl> read_decl(std::string_view w, std::size_t begin, std::size_t end, const std::vector<ir::Span>& spans) {
    Decl d;
    d.begin = begin;
    d.end   = end;
    d.text  = folded(w.substr(begin, end - begin));

    std::size_t at = begin;
    if (starts_with_word(w, at, "template")) {
        const std::size_t open = w.find('<', at);
        const std::size_t shut = open == std::string_view::npos ? open : past_close(w, open, '<', '>');
        if (shut == std::string_view::npos)
            return std::nullopt;
        at = skip_space(w, shut);
    }
    d.head_end = at;
    // Not a function: an alias, an assertion, a class head, or a friend class.
    // substrate generic algorithm: a cursor scan over the text; the position is the loop's state.
    for (const std::string_view skipped : {"using", "typedef", "static_assert", "namespace", "enum", "struct", "class", "union"})
        if (starts_with_word(w, at, skipped))
            return std::nullopt;
    if (starts_with_word(w, at, "friend")) {
        const std::size_t next = skip_space(w, at + 6);
        // substrate generic algorithm: a cursor scan over the text; the position is the loop's state.
        for (const std::string_view kind : {"class", "struct", "union"})
            if (starts_with_word(w, next, kind))
                return std::nullopt;
    }

    // The name: `operator` and what follows it up to the parameter list, or the
    // identifier before the first parenthesis.
    std::size_t open = std::string_view::npos;
    std::size_t name_begin;
    if (const std::size_t op = [&] {
            std::size_t pos = at;
            // substrate generic algorithm: finds the first whole-word `operator` before any `(`.
            while (pos < end) {
                if (w[pos] == '(')
                    return std::string_view::npos;
                if (starts_with_word(w, pos, "operator") && (pos == at || !is_word(w[pos - 1])))
                    return pos;
                ++pos;
            }
            return std::string_view::npos;
        }();
        op != std::string_view::npos) {
        name_begin             = op;
        std::size_t after_word = skip_space(w, op + 8);
        if (w.substr(after_word, 2) == "()") {
            open = after_word + 2;
            open = w.find('(', open);
            d.name = "operator()";
        } else {
            open   = w.find('(', after_word);
            if (open == std::string_view::npos || open >= end)
                return std::nullopt;
            d.name = folded(w.substr(op, open - op));
        }
    } else {
        open = w.find('(', at);
        if (open == std::string_view::npos || open >= end)
            return std::nullopt;
        std::size_t k = open;
        // substrate generic algorithm: a cursor scan over the text; the position is the loop's state.
        while (k > at && std::isspace(static_cast<unsigned char>(w[k - 1])) != 0)
            --k;
        std::size_t e = k;
        // substrate generic algorithm: a cursor scan over the text; the position is the loop's state.
        while (k > at && (is_word(w[k - 1]) || w[k - 1] == '~'))
            --k;
        if (k == e)
            return std::nullopt;
        name_begin = k;
        d.name     = std::string(w.substr(k, e - k));
    }
    if (open == std::string_view::npos || open >= end)
        return std::nullopt;
    d.name_begin = name_begin;
    d.params_end = past_close(w, open, '(', ')');
    if (d.params_end == std::string_view::npos || d.params_end > end)
        return std::nullopt;

    // The decl-specifier words before the name.
    // substrate generic algorithm: a cursor scan over the text; the position is the loop's state.
    for (std::size_t i = at; i < name_begin;) {
        if (!is_word(w[i])) {
            ++i;
            continue;
        }
        std::size_t j = i;
        // substrate generic algorithm: a cursor scan over the text; the position is the loop's state.
        while (j < name_begin && is_word(w[j]))
            ++j;
        Decl::Word word{std::string(w.substr(i, j - i)), i, j};
        if (word.word == "explicit" && skip_space(w, j) < name_begin && w[skip_space(w, j)] == '(') {
            const std::size_t closed = past_close(w, skip_space(w, j), '(', ')');
            if (closed != std::string_view::npos)
                j = closed;
            word.end = j;
        }
        d.prefix.push_back(word);
        i = j;
    }
    // substrate generic algorithm: a cursor scan over the text; the position is the loop's state.
    for (const Decl::Word& word : d.prefix) {
        d.is_constexpr = d.is_constexpr || word.word == "constexpr";
        d.is_explicit  = d.is_explicit || word.word == "explicit";
        d.is_static    = d.is_static || word.word == "static";
        d.is_friend    = d.is_friend || word.word == "friend";
    }

    // The tail after the parameter list.
    d.tail_insert = end;
    // substrate generic algorithm: a word scan of the tail.
    for (std::size_t i = d.params_end; i < end;) {
        if (starts_with_word(w, i, "noexcept") && (i == 0 || !is_word(w[i - 1]))) {
            d.is_noexcept    = true;
            d.noexcept_begin = i;
            d.noexcept_end   = i + 8;
            if (const std::size_t paren = skip_space(w, i + 8); paren < end && w[paren] == '(')
                if (const std::size_t closed = past_close(w, paren, '(', ')'); closed != std::string_view::npos)
                    d.noexcept_end = closed;
            d.noexcept_any = std::ranges::any_of(spans, [&](const ir::Span& span) {
                return span.kind == ir::SpanKind::Placeholder && span.begin >= d.noexcept_begin &&
                       span.end <= d.noexcept_end;
            });
            i = d.noexcept_end;
            continue;
        }
        if (w[i] == '-' && i + 1 < end && w[i + 1] == '>')
            d.tail_insert = std::min(d.tail_insert, i);
        if (starts_with_word(w, i, "requires"))
            d.tail_insert = std::min(d.tail_insert, i);
        if (w[i] == '=') {
            const std::size_t k = skip_space(w, i + 1);
            if (starts_with_word(w, k, "default"))
                d.is_default = true;
            if (starts_with_word(w, k, "delete"))
                d.is_delete = true;
            d.tail_insert = std::min(d.tail_insert, i);
        }
        ++i;
    }
    // Insert before the whitespace that precedes the marker, not after it.
    // substrate generic algorithm: a cursor scan over the text; the position is the loop's state.
    while (d.tail_insert > d.params_end && std::isspace(static_cast<unsigned char>(w[d.tail_insert - 1])) != 0)
        --d.tail_insert;
    return d;
}

std::vector<Decl> declarations(const ir::CodeText& code) {
    const std::string& text = code.text;
    const std::string w = blanked(text);
    std::vector<Decl> out;
    std::size_t       begin = 0;
    int               parens = 0;
    // substrate generic algorithm: a statement scan; the start and the depth are the loop's state.
    for (std::size_t i = 0; i < w.size(); ++i) {
        const char c = w[i];
        if (c == '(')
            ++parens;
        else if (c == ')')
            --parens;
        if (parens > 0 || (c != ';' && c != '{' && c != '}'))
            continue;
        if (c == ';') {
            const std::size_t first = skip_space(w, begin);
            if (first < i)
                if (auto d = read_decl(w, first, i, code.spans))
                    out.push_back(std::move(*d));
        }
        begin  = i + 1;
        parens = 0;
    }
    return out;
}

std::vector<Decl::Word>::const_iterator named(const std::vector<Decl::Word>& words, std::string_view word) {
    return std::ranges::find_if(words, [&](const Decl::Word& w) { return w.word == word; });
}

struct Pairing {
    std::vector<std::pair<std::size_t, std::size_t>> pairs; // (synopsis, declared)
    std::vector<std::size_t>                         only_synopsis;
    std::vector<std::size_t>                         only_declared;
};

// By name; among declarations of one name, the pairs that read most alike first.
Pairing pair_up(const std::vector<Decl>& synopsis, const std::vector<Decl>& declared) {
    struct Candidate {
        std::size_t s;
        std::size_t d;
        double      score;
    };
    std::vector<Candidate> candidates;
    // substrate generic algorithm: the cross product of the declarations that share a name, scored.
    for (std::size_t s = 0; s < synopsis.size(); ++s)
        // substrate generic algorithm: a cursor scan over the text; the position is the loop's state.
        for (std::size_t d = 0; d < declared.size(); ++d)
            if (synopsis[s].name == declared[d].name)
                candidates.push_back({s, d, diff::declaration_similarity({synopsis[s].text, {}}, {declared[d].text, {}})});
    std::ranges::stable_sort(candidates, std::greater<>{}, &Candidate::score);

    Pairing           out;
    std::vector<bool> took_s(synopsis.size(), false);
    std::vector<bool> took_d(declared.size(), false);
    // substrate generic algorithm: claims the candidates best first, writing both tables.
    for (const Candidate& c : candidates) {
        if (!took_s[c.s] && !took_d[c.d]) {
            took_s[c.s] = took_d[c.d] = true;
            out.pairs.emplace_back(c.s, c.d);
        }
    }
    std::ranges::sort(out.pairs);
    // substrate generic algorithm: a cursor scan over the text; the position is the loop's state.
    for (std::size_t s = 0; s < synopsis.size(); ++s)
        if (!took_s[s])
            out.only_synopsis.push_back(s);
    // substrate generic algorithm: a cursor scan over the text; the position is the loop's state.
    for (std::size_t d = 0; d < declared.size(); ++d)
        if (!took_d[d])
            out.only_declared.push_back(d);
    return out;
}

struct Specifier {
    std::string_view name;
    bool Decl::*     flag;
    bool             patchable;
};

constexpr Specifier kSpecifiers[] = {
    {"constexpr", &Decl::is_constexpr, true},
    {"explicit", &Decl::is_explicit, true},
    {"noexcept", &Decl::is_noexcept, true},
    {"static", &Decl::is_static, false},
    {"friend", &Decl::is_friend, false},
    {"= default", &Decl::is_default, false},
    {"= delete", &Decl::is_delete, false},
};

std::vector<Finding> disagreements(const Decl& s, const Decl& d, bool only_unpatchable) {
    std::vector<Finding> out;
    // substrate generic algorithm: a cursor scan over the text; the position is the loop's state.
    for (const Specifier& spec : kSpecifiers) {
        if (only_unpatchable && spec.patchable)
            continue;
        const bool in_synopsis = s.*spec.flag;
        const bool in_code     = d.*spec.flag;
        if (spec.flag == &Decl::is_noexcept && (s.noexcept_any || d.noexcept_any))
            continue;
        if (in_synopsis != in_code)
            out.push_back({s.text,
                           std::format("the synopsis {} `{}` and the declaration {}",
                                       in_synopsis ? "has" : "does not have",
                                       spec.name,
                                       in_code ? "does" : "does not")});
    }
    return out;
}

} // namespace

std::vector<Finding> compare(const ir::CodeText& synopsis, const ir::CodeText& declared) {
    const std::vector<Decl> s       = declarations(synopsis);
    const std::vector<Decl> d       = declarations(declared);
    const Pairing           pairing = pair_up(s, d);

    std::vector<Finding> out;
    // substrate generic algorithm: collects each pair's findings, in synopsis order.
    for (const auto& [si, di] : pairing.pairs)
        out.append_range(disagreements(s[si], d[di], false));
    // substrate generic algorithm: a cursor scan over the text; the position is the loop's state.
    for (const std::size_t i : pairing.only_synopsis)
        out.push_back({s[i].text, "the synopsis declares this, and the code does not"});
    // substrate generic algorithm: a cursor scan over the text; the position is the loop's state.
    for (const std::size_t i : pairing.only_declared)
        out.push_back({d[i].text, "the code declares this, and the synopsis does not"});
    return out;
}

Patched patch(const ir::CodeText& synopsis, const ir::CodeText& declared) {
    const std::string       w = blanked(synopsis.text);
    const std::vector<Decl> s = declarations(synopsis);
    const std::vector<Decl> d = declarations(declared);
    const Pairing           pairing = pair_up(s, d);

    struct Edit {
        std::size_t pos;
        std::size_t remove;
        std::string insert;
    };
    std::vector<Edit>    edits;
    std::vector<Finding> unpatched;

    // The decl-specifier word a new `constexpr` or `explicit` goes before: the
    // first that is not itself one of the words the draft puts ahead of them.
    const auto specifier_point = [&](const Decl& decl, std::vector<std::string_view> ahead_of) {
        const auto found = std::ranges::find_if(decl.prefix, [&](const Decl::Word& word) {
            return std::ranges::find(ahead_of, word.word) == ahead_of.end();
        });
        return found == decl.prefix.end() ? decl.name_begin : found->begin;
    };
    const auto remove_word = [&](const Decl::Word& word) {
        std::size_t end = word.end;
        // substrate generic algorithm: a cursor scan over the text; the position is the loop's state.
        while (end < w.size() && w[end] == ' ')
            ++end;
        edits.push_back({word.begin, end - word.begin, {}});
    };

    // substrate generic algorithm: each paired declaration contributes its edits.
    for (const auto& [si, di] : pairing.pairs) {
        const Decl& sd = s[si];
        const Decl& dd = d[di];
        if (sd.is_constexpr != dd.is_constexpr) {
            if (dd.is_constexpr)
                edits.push_back({specifier_point(sd, {"friend", "static", "virtual", "inline"}), 0, "constexpr "});
            else
                remove_word(*named(sd.prefix, "constexpr"));
        }
        if (sd.is_explicit != dd.is_explicit) {
            if (dd.is_explicit) {
                const auto word = named(dd.prefix, "explicit");
                edits.push_back({specifier_point(sd, {"friend", "static", "virtual", "inline", "constexpr"}),
                                 0,
                                 declared.text.substr(word->begin, word->end - word->begin) + " "});
            } else {
                remove_word(*named(sd.prefix, "explicit"));
            }
        }
        if (sd.is_noexcept != dd.is_noexcept && !sd.noexcept_any && !dd.noexcept_any) {
            if (dd.is_noexcept)
                edits.push_back(
                    {sd.tail_insert, 0, " " + declared.text.substr(dd.noexcept_begin, dd.noexcept_end - dd.noexcept_begin)});
            else {
                std::size_t begin = sd.noexcept_begin;
                // substrate generic algorithm: a cursor scan over the text; the position is the loop's state.
                while (begin > sd.params_end && w[begin - 1] == ' ')
                    --begin;
                edits.push_back({begin, sd.noexcept_end - begin, {}});
            }
        }
        unpatched.append_range(disagreements(sd, dd, true));
    }
    // substrate generic algorithm: a cursor scan over the text; the position is the loop's state.
    for (const std::size_t i : pairing.only_synopsis)
        unpatched.push_back({s[i].text, "the synopsis declares this, and the code does not"});
    // substrate generic algorithm: a cursor scan over the text; the position is the loop's state.
    for (const std::size_t i : pairing.only_declared)
        unpatched.push_back({d[i].text, "the code declares this, and the synopsis does not"});

    std::ranges::stable_sort(edits, {}, &Edit::pos);

    Patched out;
    out.unpatched = std::move(unpatched);
    out.code      = synopsis;
    // Apply back to front so each offset is still the original's; then move the
    // spans by what was inserted or removed before them.
    // substrate generic algorithm: a scatter of edits into the text, which mutates it.
    for (const Edit& edit : edits | std::views::reverse)
        out.code.text.replace(edit.pos, edit.remove, edit.insert);
    const auto moved = [&](std::size_t old) {
        std::ptrdiff_t shift = 0;
        // substrate generic algorithm: a sum over the edits that end at or before `old`.
        for (const Edit& edit : edits)
            if (edit.pos + edit.remove <= old)
                shift += static_cast<std::ptrdiff_t>(edit.insert.size()) - static_cast<std::ptrdiff_t>(edit.remove);
        return static_cast<std::size_t>(static_cast<std::ptrdiff_t>(old) + shift);
    };
    // substrate generic algorithm: a cursor scan over the text; the position is the loop's state.
    for (ir::Span& span : out.code.spans) {
        span.begin = moved(span.begin);
        span.end   = moved(span.end);
    }
    return out;
}

} // namespace beman::specgen::synopsis_check
