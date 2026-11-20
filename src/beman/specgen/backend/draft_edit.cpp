// src/beman/specgen/backend/draft_edit.cpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/backend/draft_edit.hpp>

#include <beman/specgen/backend/draft_spans.hpp>
#include <beman/specgen/backend/latex.hpp>
#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/diff.hpp>
#include <beman/specgen/diff/sequence.hpp>
#include <beman/specgen/draft_import.hpp>
#include <beman/specgen/ir.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <expected>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace beman::specgen::backend::draft_edit {

namespace {

// --- code tokens ----------------------------------------------------------------

// A token of code: a span whole, a run of word characters, or one other
// character. Whitespace separates tokens and is not one, which is what makes a
// line break, an indentation or a space no change.
struct Token {
    std::size_t     begin = 0;
    std::size_t     end   = 0;
    const ir::Span* span  = nullptr;
};

bool is_word(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool is_space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

std::vector<Token> tokens_of(const ir::CodeText& code) {
    std::vector<Token> out;
    const std::string& text = code.text;
    auto               span = code.spans.begin();
    std::size_t        i    = 0;
    // substrate generic algorithm: a scan whose step is a span, a word or a
    // character; the position and the next span are its state.
    while (i < text.size()) {
        // substrate generic algorithm: skips the spans the scan has passed.
        while (span != code.spans.end() && span->begin < i)
            ++span;
        if (span != code.spans.end() && span->begin == i && span->end > i) {
            out.push_back({span->begin, span->end, &*span});
            i = span->end;
            continue;
        }
        if (is_space(text[i])) {
            ++i;
            continue;
        }
        std::size_t end = i + 1;
        if (is_word(text[i])) {
            // substrate generic algorithm: a cursor scan over a word.
            while (end < text.size() && is_word(text[end]) && (span == code.spans.end() || end < span->begin))
                ++end;
        }
        out.push_back({i, end, nullptr});
        i = end;
    }
    return out;
}

struct SameToken {
    const ir::CodeText& a;
    const ir::CodeText& b;

    // An index entry around a name is the name (`ir::is_wording`): the draft's
    // choice of whether a head carries one stands.
    bool operator()(const Token& x, const Token& y) const {
        const ir::Span* xs = x.span != nullptr && ir::is_wording(*x.span) ? x.span : nullptr;
        const ir::Span* ys = y.span != nullptr && ir::is_wording(*y.span) ? y.span : nullptr;
        if ((xs == nullptr) != (ys == nullptr))
            return false;
        if (xs != nullptr && (xs->kind != ys->kind || xs->payload != ys->payload))
            return false;
        return std::string_view(a.text).substr(x.begin, x.end - x.begin) ==
               std::string_view(b.text).substr(y.begin, y.end - y.begin);
    }
};

// Typed listing text has its escapes as written; read it as the importer
// reads the draft's, so the two compare token for token.
ir::CodeText as_imported(const ir::CodeText& code) {
    return code.spans.empty() && code.text.contains('@') ? draft_import::parse_listing(code.text) : code;
}

// The paper's code [begin, end) as the draft writes it in a listing: a span in
// its escape, whitespace with a line break in it one space, since the edit
// sits in the draft's own layout. Nothing for a slice that cuts a span.
std::optional<std::string>
listing_text(const ir::CodeText& code, std::size_t begin, std::size_t end, bool fold = true) {
    std::string out;
    std::size_t pos = begin;
    // substrate generic algorithm: the position-tracking span walk of
    // code_spans.hpp, over one slice.
    for (const ir::Span& span : code.spans) {
        if (span.end <= begin || span.begin >= end)
            continue;
        if (span.begin < begin || span.end > end)
            return std::nullopt;
        out += draft_listing_text(std::string_view(code.text).substr(pos, span.begin - pos));
        out += draft_span_codeblock(span, std::string_view(code.text).substr(span.begin, span.end - span.begin));
        pos = span.end;
    }
    out += draft_listing_text(std::string_view(code.text).substr(pos, end - pos));
    if (!fold)
        return out;
    std::string folded;
    // substrate generic algorithm: folds each run of whitespace that holds a
    // line break to one space, and keeps the rest as written.
    for (std::size_t i = 0; i < out.size();) {
        if (!is_space(out[i])) {
            folded.push_back(out[i++]);
            continue;
        }
        std::size_t j = i;
        // substrate generic algorithm: a cursor scan over a run of whitespace.
        while (j < out.size() && is_space(out[j]))
            ++j;
        const std::string_view run = std::string_view(out).substr(i, j - i);
        folded += run.contains('\n') ? std::string(" ") : std::string(run);
        i = j;
    }
    return folded;
}

// --- placing code changes ----------------------------------------------------------

// Where the line holding `pos` starts.
std::size_t line_start(std::string_view text, std::size_t pos) {
    const std::size_t newline = text.rfind('\n', pos == 0 ? 0 : pos - 1);
    return pos == 0 || newline == std::string_view::npos ? 0 : newline + 1;
}

// Whether only spaces come before `pos` on its line.
bool starts_line(std::string_view text, std::size_t pos) {
    return std::ranges::all_of(text.substr(line_start(text, pos), pos - line_start(text, pos)),
                               [](char c) { return c == ' ' || c == '\t'; });
}

struct Collector {
    std::map<std::string, std::vector<Edit>> edits;
    std::vector<Finding>                     findings;
    // The draft's source, by file, for the edits that have to see it: which
    // paragraphs of a description changed, and the blank line after a node
    // that goes. Read once each.
    Reader                                                         read    = {};
    std::map<std::string, std::optional<std::string>, std::less<>> sources = {};

    void error(std::string where, std::string message) {
        findings.push_back({Severity::Error, std::move(where), std::move(message)});
    }

    std::optional<std::string_view> text_of(const std::string& file) {
        auto found = sources.find(file);
        if (found == sources.end())
            found = sources.emplace(file, read ? read(file) : std::nullopt).first;
        if (!found->second)
            return std::nullopt;
        return std::string_view(*found->second);
    }
};

// The edits that turn `draft`'s code into `paper`'s, in the file `draft` was
// read from.
void place_code(const ir::CodeText& draft, const ir::CodeText& paper_code, const std::string& where, Collector& out) {
    const ir::CodeText       paper  = as_imported(paper_code);
    const std::vector<Token> dt     = tokens_of(draft);
    const std::vector<Token> pt     = tokens_of(paper);
    const auto               script = diff::edit_script(dt, pt, SameToken{draft, paper});
    if (std::ranges::all_of(script, [](const diff::Edit& e) { return e.op == diff::Op::Keep; }))
        return;
    const ir::CodeSource* source = draft.origin.source.get();
    if (source == nullptr || source->bytes.size() != draft.text.size() || draft.text.empty()) {
        out.error(where, "the draft's code here was not read from a file, so the change cannot be placed in it");
        return;
    }
    const std::string_view text = draft.text;
    // Where a draft text offset is in the file: the start of the byte there, or
    // the end of the last byte for the end of the text.
    const auto file_at = [&](std::size_t pos) {
        return pos < source->bytes.size() ? source->bytes[pos].begin : source->bytes.back().end;
    };
    const auto file_end = [&](std::size_t pos) { return pos == 0 ? file_at(0) : source->bytes[pos - 1].end; };
    // Whether the paper's text has whitespace between its tokens i - 1 and i.
    const auto paper_space = [&](std::size_t i) { return i > 0 && i < pt.size() && pt[i - 1].end < pt[i].begin; };

    // The paper's lines that hold its tokens [b0, b1) and nothing else, with
    // the blank lines after them, as the draft writes a listing; nothing when
    // another token shares a line with them.
    const auto whole_lines = [&](std::size_t b0, std::size_t b1) -> std::optional<std::string> {
        const std::string_view ptext = paper.text;
        if (b1 >= pt.size() ||
            !(b0 == 0 || ptext.substr(pt[b0 - 1].end, pt[b0].begin - pt[b0 - 1].end).contains('\n')) ||
            !ptext.substr(pt[b1 - 1].end, pt[b1].begin - pt[b1 - 1].end).contains('\n'))
            return std::nullopt;
        return listing_text(paper, line_start(ptext, pt[b0].begin), line_start(ptext, pt[b1].begin), false);
    };

    std::size_t k = 0;
    // substrate generic algorithm: each maximal run of the script's changes
    // becomes one edit; the position in the script is the loop's state.
    while (k < script.size()) {
        if (script[k].op == diff::Op::Keep) {
            ++k;
            continue;
        }
        const std::size_t run_begin = k;
        // substrate generic algorithm: a cursor scan to the end of the run.
        while (k < script.size() && script[k].op != diff::Op::Keep)
            ++k;
        const std::span<const diff::Edit> run(script.data() + run_begin, k - run_begin);
        const auto deleted  = run | std::views::filter([](const diff::Edit& e) { return e.op == diff::Op::Delete; }) |
                              std::views::transform(&diff::Edit::a) | std::ranges::to<std::vector>();
        const auto inserted = run | std::views::filter([](const diff::Edit& e) { return e.op == diff::Op::Insert; }) |
                              std::views::transform(&diff::Edit::b) | std::ranges::to<std::vector>();
        // The draft tokens [a0, a1) go, and the paper tokens [b0, b1) come.
        const std::size_t a0 = run.front().a;
        const std::size_t a1 = a0 + deleted.size();
        const std::size_t b0 = run.front().b;
        const std::size_t b1 = b0 + inserted.size();

        std::optional<std::string> added;
        if (!inserted.empty()) {
            added = listing_text(paper, pt[b0].begin, pt[b1 - 1].end);
            if (!added) {
                out.error(where, "a change cuts an exposition-only name or a placeholder, so it cannot be placed");
                continue;
            }
        }
        std::size_t begin = 0;
        std::size_t end   = 0;
        std::string replacement;
        if (!deleted.empty()) {
            begin       = dt[a0].begin;
            end         = dt[a1 - 1].end;
            replacement = added.value_or("");
            if (inserted.empty()) {
                // A token removed takes the spaces after it on its line, or,
                // when none follow it there, the ones before it.
                const std::size_t next   = a1 < dt.size() ? dt[a1].begin : text.size();
                const auto        after  = text.substr(end, next - end);
                const std::size_t prev   = a0 > 0 ? dt[a0 - 1].end : begin;
                const auto        before = text.substr(prev, begin - prev);
                if (!after.empty() && !after.contains('\n') && a1 < dt.size())
                    end = next;
                else if (!before.empty() && !before.contains('\n'))
                    begin = prev;
            }
        } else if (const auto lines = whole_lines(b0, b1);
                   lines && a0 < dt.size() && starts_line(text, dt[a0].begin)) {
            // Tokens the paper writes on lines of their own, before a token
            // that starts its line in the draft, are lines of their own there
            // too, as the paper lays them out: run onto the draft's line, an
            // access specifier ends up in a comment's text (issue #378).
            begin = end = line_start(text, dt[a0].begin);
            replacement = *lines;
        } else if (a0 < dt.size()) {
            begin = end = dt[a0].begin;
            replacement = *added + (paper_space(b1) || b1 == pt.size() ? " " : "");
        } else {
            begin = end = dt.empty() ? 0 : dt.back().end;
            replacement = (paper_space(b0) ? " " : "") + *added;
        }
        // Added tokens the paper separates from the token before them, or
        // after them, by a space are separated so in the draft too, where
        // nothing already does (issue #378): `expected()` gaining a
        // `noexcept` reads `expected() noexcept`, not `expected()noexcept`.
        if (added && !replacement.empty() && !replacement.ends_with('\n')) {
            if (paper_space(b0) && begin > 0 && !is_space(text[begin - 1]) && !is_space(replacement.front()))
                replacement.insert(0, 1, ' ');
            if (paper_space(b1) && end < text.size() && !is_space(text[end]) && !is_space(replacement.back()))
                replacement.push_back(' ');
        }
        out.edits[source->file].push_back(
            {file_at(begin), begin == end ? file_at(begin) : file_end(end), replacement});
    }
}

// --- placing whole nodes and descriptions (issue #346) ----------------------------

// A node as the LaTeX backend writes it; a subclause at the `\rSec` level
// given.
std::string latex_of(const ir::Node& node, int level = 3) {
    ir::Document document;
    document.nodes.push_back(node);
    return latex::render_to_string(document, {.base_section_depth = level});
}

// The `\rSec` level of the subclause read from `origin`, from its own line.
std::optional<int> rsec_level(const ir::NodeOrigin& origin, Collector& out);

// LaTeX laid out as the draft's source is: a sentence to a line. The backend
// writes a paragraph on one line; a line of prose is cut after each `. ` that
// ends a sentence, outside every group, where the next sentence begins with a
// capital or a macro. A code block's lines are its own.
std::string draft_layout(std::string_view latex) {
    std::string out;
    bool        in_code = false;
    // substrate generic algorithm: a line-by-line rewrite whose state is
    // whether the line is inside a code block.
    for (const auto part : latex | std::views::split('\n')) {
        const std::string_view line(part.begin(), part.end());
        if (line.starts_with("\\begin{codeblock}") || line.starts_with("\\begin{itemdecl}"))
            in_code = true;
        if (in_code) {
            out += line;
        } else {
            int depth = 0;
            // substrate generic algorithm: a scan of one line that copies it,
            // breaking it after each sentence; the brace depth is its state.
            for (std::size_t i = 0; i < line.size(); ++i) {
                const char c = line[i];
                if (c == '\\' && i + 1 < line.size()) {
                    out += line.substr(i, 2);
                    ++i;
                    continue;
                }
                depth += c == '{' ? 1 : c == '}' ? -1 : 0;
                if (c == ' ' && depth == 0 && i > 0 && line[i - 1] == '.' && i + 1 < line.size() &&
                    (std::isupper(static_cast<unsigned char>(line[i + 1])) != 0 || line[i + 1] == '\\')) {
                    out += '\n';
                    continue;
                }
                out += c;
            }
        }
        if (line.starts_with("\\end{codeblock}") || line.starts_with("\\end{itemdecl}"))
            in_code = false;
        out += '\n';
    }
    // The split leaves one more line than the text had.
    if (out.ends_with("\n\n") && !latex.ends_with("\n\n"))
        out.pop_back();
    return out;
}

// The text of a description as the LaTeX backend writes it, without the
// `itemdescr` around it: what the draft's source has between
// `\begin{itemdescr}` and `\end{itemdescr}`, or the whole of prose that is no
// item's.
std::string description_latex(const ir::SpecItem& item) {
    const std::string          whole = latex_of(item);
    constexpr std::string_view open  = "\\begin{itemdescr}\n";
    constexpr std::string_view close = "\\end{itemdescr}";
    const std::size_t          begin = whole.find(open);
    const std::size_t          end   = whole.rfind(close);
    if (begin == std::string::npos || end == std::string::npos || end < begin)
        return whole;
    return whole.substr(begin + open.size(), end - begin - open.size());
}

// A description cut into its numbered paragraphs: before each line that is a
// `\pnum`. What comes before the first is a piece of its own.
std::vector<std::string_view> numbered_paragraphs(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t                   from = 0;
    std::size_t                   at   = 0;
    // substrate generic algorithm: a scan for each line that is `\pnum`.
    while ((at = text.find("\\pnum\n", at)) != std::string_view::npos) {
        if (at == 0 || text[at - 1] == '\n') {
            if (at > from)
                out.push_back(text.substr(from, at - from));
            from = at;
        }
        at += 6;
    }
    out.push_back(text.substr(from));
    return out;
}

// Text with each run of whitespace one space and none at either end, and for
// each byte of it, the byte of `text` it came from.
std::pair<std::string, std::vector<std::size_t>> normalized(std::string_view text) {
    std::string              out;
    std::vector<std::size_t> from;
    bool                     space = false;
    // substrate generic algorithm: a scan that copies the text, folding each
    // run of whitespace; whether one is pending is its state.
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (std::isspace(static_cast<unsigned char>(text[i])) != 0) {
            space = !out.empty();
            continue;
        }
        if (std::exchange(space, false)) {
            out += ' ';
            from.push_back(i);
        }
        out += text[i];
        from.push_back(i);
    }
    return {std::move(out), std::move(from)};
}

// The edits within one numbered paragraph that changed (issue #346). The
// draft's paragraph as the generator writes it and the paper's are each laid
// out a sentence to a line, and the lines compared: a line the paper keeps is
// left as the draft's source has it, byte for byte, and only the lines that
// changed are replaced, by the paper's. That needs the draft's source to say
// what the generator says, up to its line breaks; where it does not (a `%`
// comment, a macro spelled another way), the paragraph is replaced whole.
void place_paragraph(std::string_view   source,
                     std::size_t        offset,
                     std::string_view   before,
                     std::string_view   mine,
                     const std::string& file,
                     Collector&         out) {
    const auto lines_of = [](std::string_view latex) {
        return draft_layout(latex) | std::views::split('\n') |
               std::views::transform([](auto&& line) { return std::string(line.begin(), line.end()); }) |
               std::views::filter([](const std::string& line) { return !line.empty(); }) |
               std::ranges::to<std::vector>();
    };
    const std::vector<std::string> was = lines_of(before);
    const std::vector<std::string> is  = lines_of(mine);
    const auto [flat, from]            = normalized(source);
    const auto starts = was | std::views::transform([](const std::string& line) { return normalized(line).first; }) |
                        std::ranges::to<std::vector>();
    const std::string joined = starts | std::views::join_with(' ') | std::ranges::to<std::string>();
    if (joined != flat || was.empty()) {
        out.edits[file].push_back({offset, offset + source.size(), draft_layout(mine)});
        return;
    }
    // Where each of the draft's lines is in the source: [begin, end).
    std::vector<std::pair<std::size_t, std::size_t>> where;
    std::size_t                                      at = 0;
    // substrate generic algorithm: each line's place, one after the other.
    for (const std::string& line : starts) {
        where.emplace_back(from[at], from[at + line.size() - 1] + 1);
        at += line.size() + 1;
    }
    const auto  script = diff::edit_script(was, is);
    std::size_t k      = 0;
    // substrate generic algorithm: each maximal run of the script's changes
    // becomes one edit; the position in the script is the loop's state.
    while (k < script.size()) {
        if (script[k].op == diff::Op::Keep) {
            ++k;
            continue;
        }
        const std::size_t first = k;
        // substrate generic algorithm: a cursor scan to the end of the run.
        while (k < script.size() && script[k].op != diff::Op::Keep)
            ++k;
        const std::span<const diff::Edit> run(script.data() + first, k - first);
        const auto deleted = run | std::views::filter([](const diff::Edit& e) { return e.op == diff::Op::Delete; }) |
                             std::views::transform(&diff::Edit::a) | std::ranges::to<std::vector>();
        const std::string added = run |
                                  std::views::filter([](const diff::Edit& e) { return e.op == diff::Op::Insert; }) |
                                  std::views::transform([&](const diff::Edit& e) { return is[e.b]; }) |
                                  std::views::join_with('\n') | std::ranges::to<std::string>();
        if (!deleted.empty()) {
            std::size_t begin = where[deleted.front()].first;
            std::size_t end   = where[deleted.back()].second;
            if (added.empty()) {
                // A line that goes takes its line break, or the space after it.
                // substrate generic algorithm: a cursor scan over trailing spaces.
                while (end < source.size() && source[end] == ' ')
                    ++end;
                if (end < source.size() && source[end] == '\n' && (begin == 0 || source[begin - 1] == '\n'))
                    ++end;
            }
            out.edits[file].push_back({offset + begin, offset + end, added});
        } else if (run.front().a > 0) {
            const std::size_t after = where[run.front().a - 1].second;
            out.edits[file].push_back({offset + after, offset + after, '\n' + added});
        } else {
            const std::size_t ahead = where.front().first;
            out.edits[file].push_back({offset + ahead, offset + ahead, added + '\n'});
        }
    }
}

// The edits that make the draft's description of `draft` the paper's. Where
// the two have as many numbered paragraphs, only those that changed are
// replaced, each in the draft's source with the paper's paragraph laid out as
// the draft's are; otherwise the description is replaced whole.
void place_description(const ir::SpecItem& draft,
                       const ir::SpecItem& paper,
                       const std::string&  where,
                       Collector&          out) {
    const ir::SourceRange* range = draft.descr_origin.range.get();
    if (range == nullptr) {
        out.error(where,
                  "the draft's description here was not read from a file, so the change cannot be placed in it");
        return;
    }
    const std::string ours   = description_latex(paper);
    const std::string theirs = description_latex(draft);
    const auto        text   = out.text_of(range->file);
    const auto        source = text && range->end <= text->size()
                                   ? std::optional(text->substr(range->begin, range->end - range->begin))
                                   : std::nullopt;
    const auto        mine   = numbered_paragraphs(ours);
    const auto        before = numbered_paragraphs(theirs);
    const auto        lines  = source ? numbered_paragraphs(*source) : std::vector<std::string_view>{};
    if (!source || mine.size() != before.size() || lines.size() != before.size()) {
        out.edits[range->file].push_back({range->begin, range->end, draft_layout(ours)});
        return;
    }
    std::size_t offset = range->begin;
    // substrate generic algorithm: each numbered paragraph, kept or replaced in place.
    for (std::size_t k = 0; k < lines.size(); ++k) {
        if (mine[k] != before[k])
            place_paragraph(lines[k], offset, before[k], mine[k], range->file, out);
        offset += lines[k].size();
    }
}

// The edit that puts `node`, which the paper adds, after `anchor`'s place in
// the draft's source, or before it when `before` is set. A blank line
// separates it from its neighbour, as the draft's items are separated. A
// subclause is written at `level`, which says where it stands in the draft.
bool place_added(
    const ir::Node& node, const ir::NodeOrigin& anchor, bool before, std::optional<int> level, Collector& out) {
    const ir::SourceRange* range = anchor.range.get();
    if (range == nullptr || (std::holds_alternative<ir::Section>(node) && !level))
        return false;
    std::string written = draft_layout(latex_of(node, level.value_or(3)));
    if (!written.ends_with('\n'))
        written += '\n';
    if (before)
        out.edits[range->file].push_back({range->begin, range->begin, written + '\n'});
    else
        out.edits[range->file].push_back({range->end, range->end, '\n' + written});
    return true;
}

// Where the `%` comment lines directly above `begin` start: the comment the
// draft wrote over a node, with no blank line between, belongs to it.
std::size_t attached_comment(std::string_view text, std::size_t begin) {
    // substrate generic algorithm: a search back, one line at a time, over the
    // comment lines directly above.
    while (begin > 0 && text[begin - 1] == '\n') {
        const std::size_t previous = begin >= 2 ? text.rfind('\n', begin - 2) : std::string_view::npos;
        const std::size_t start    = previous == std::string_view::npos ? 0 : previous + 1;
        const auto        line     = text.substr(start, begin - 1 - start);
        const auto        first    = line.find_first_not_of(" \t");
        if (first == std::string_view::npos || line[first] != '%')
            break;
        begin = start;
    }
    return begin;
}

// The edit that takes a node the paper removes out of the draft's source,
// with the blank line after it and the comment directly above it.
bool place_removed(const ir::NodeOrigin& origin, Collector& out) {
    const ir::SourceRange* range = origin.range.get();
    if (range == nullptr)
        return false;
    std::size_t begin = range->begin;
    std::size_t end   = range->end;
    const auto  text  = out.text_of(range->file);
    if (text && end < text->size() && (*text)[end] == '\n')
        ++end;
    if (text)
        begin = attached_comment(*text, begin);
    out.edits[range->file].push_back({begin, end, {}});
    return true;
}

std::optional<int> rsec_level(const ir::NodeOrigin& origin, Collector& out) {
    const ir::SourceRange* range = origin.range.get();
    const auto             text  = range == nullptr ? std::nullopt : out.text_of(range->file);
    if (!text || range->begin + 6 > text->size() || text->substr(range->begin, 5) != "\\rSec")
        return std::nullopt;
    const char digit = (*text)[range->begin + 5];
    if (digit < '0' || digit > '9')
        return std::nullopt;
    return digit - '0';
}

// Where a node was read from, when it says.
ir::NodeOrigin origin_of(const ir::Node& node) {
    if (const auto* section = std::get_if<ir::Section>(&node))
        return section->origin;
    if (const auto* item = std::get_if<ir::SpecItem>(&node))
        return item->origin;
    if (const auto* synopsis = std::get_if<ir::Synopsis>(&node))
        return synopsis->origin;
    return {};
}

// --- pairing -----------------------------------------------------------------------

std::string describe(const ir::Node& node) {
    if (const auto* section = std::get_if<ir::Section>(&node))
        return std::format("the subclause [{}]", section->stable_name);
    if (const auto* synopsis = std::get_if<ir::Synopsis>(&node))
        return synopsis->name.empty() ? std::string("a synopsis")
                                      : std::format("the synopsis of `{}`", synopsis->name);
    if (const auto* item = std::get_if<ir::SpecItem>(&node))
        return item->decl.signatures.empty() ? std::string("a description with no declaration")
                                             : std::format("the item for `{}`", item->decl.signatures.front().text);
    return std::string("a paragraph");
}

std::vector<std::string> names_of(const ir::SpecItem& item) {
    std::vector<std::string> names = item.decl.entities;
    std::ranges::sort(names);
    names.erase(std::ranges::unique(names).begin(), names.end());
    return names;
}

double alike(const ir::CodeText& a, const ir::CodeText& b) {
    const ir::CodeText x = as_imported(a);
    const ir::CodeText y = as_imported(b);
    return ir::without_typesetting(x) == ir::without_typesetting(y) ? 1.0 : diff::declaration_similarity(x, y);
}

double items_alike(const ir::SpecItem& a, const ir::SpecItem& b) {
    const auto scores =
        std::views::cartesian_product(a.decl.signatures, b.decl.signatures) |
        std::views::transform([](const auto& pair) { return alike(std::get<0>(pair), std::get<1>(pair)); });
    return std::ranges::fold_left(scores, 0.0, [](double best, double s) { return std::max(best, s); });
}

// The class a synopsis is of: its name, or, for a typed synopsis that has
// none, the class its code declares, as the differ names one.
std::string class_of(const ir::Synopsis& synopsis) {
    return synopsis.name.empty() ? draft_import::declared_class_name(as_imported(synopsis.code).text) : synopsis.name;
}

// How alike two declarations must read to pair when one side names nothing:
// the differ's threshold, as inheritance uses it.
constexpr double kReadingThreshold = 0.5;

// The draft node each paper node pairs with, by index, or nothing.
std::vector<std::optional<std::size_t>> pair_nodes(const std::vector<ir::Node>& draft,
                                                   const std::vector<ir::Node>& paper) {
    std::vector<std::optional<std::size_t>> out(paper.size());
    std::vector<bool>                       claimed(draft.size(), false);
    const auto                              claim_best = [&](std::size_t p, auto&& score, double minimum) {
        std::optional<std::size_t> best;
        double                     best_score = 0.0;
        // substrate generic algorithm: an argmax over the unclaimed draft
        // nodes, the first of equals, which writes the claim table.
        for (std::size_t d = 0; d < draft.size(); ++d) {
            if (claimed[d])
                continue;
            const std::optional<double> s = score(draft[d]);
            if (s && *s >= minimum && (!best || *s > best_score)) {
                best       = d;
                best_score = *s;
            }
        }
        if (best) {
            claimed[*best] = true;
            out[p]         = best;
        }
    };
    // substrate generic algorithm: each paper node claims its draft node, in
    // two passes so that a pairing by name is never lost to one by reading.
    for (int pass = 0; pass < 2; ++pass) {
        // substrate generic algorithm: the paper nodes still unpaired, in order.
        for (std::size_t p = 0; p < paper.size(); ++p) {
            if (out[p])
                continue;
            const ir::Node& node = paper[p];
            if (const auto* section = std::get_if<ir::Section>(&node)) {
                claim_best(
                    p,
                    [&](const ir::Node& d) -> std::optional<double> {
                        const auto* s = std::get_if<ir::Section>(&d);
                        return s != nullptr && s->stable_name == section->stable_name ? std::optional<double>{1.0}
                                                                                      : std::nullopt;
                    },
                    0.0);
            } else if (const auto* synopsis = std::get_if<ir::Synopsis>(&node)) {
                const std::string name = class_of(*synopsis);
                claim_best(
                    p,
                    [&](const ir::Node& d) -> std::optional<double> {
                        const auto* s = std::get_if<ir::Synopsis>(&d);
                        if (s == nullptr || class_of(*s) != name)
                            return std::nullopt;
                        return pass == 0 ? alike(s->code, synopsis->code) : 0.0;
                    },
                    0.0);
            } else if (const auto* item = std::get_if<ir::SpecItem>(&node)) {
                const std::vector<std::string> names = names_of(*item);
                claim_best(
                    p,
                    [&](const ir::Node& d) -> std::optional<double> {
                        const auto* other = std::get_if<ir::SpecItem>(&d);
                        if (other == nullptr || item->decl.signatures.empty() != other->decl.signatures.empty())
                            return std::nullopt;
                        // A description with no declaration pairs with one, in order.
                        if (item->decl.signatures.empty())
                            return 1.0;
                        const std::vector<std::string> theirs = names_of(*other);
                        if (pass == 0)
                            return !names.empty() && theirs == names ? std::optional(items_alike(*item, *other))
                                                                     : std::nullopt;
                        return names.empty() || theirs.empty() ? std::optional(items_alike(*item, *other))
                                                               : std::nullopt;
                    },
                    pass == 0 ? 0.0 : kReadingThreshold);
            } else if (pass == 0) {
                claim_best(
                    p,
                    [](const ir::Node& d) -> std::optional<double> {
                        return std::holds_alternative<ir::FreeParagraph>(d) ? std::optional<double>{1.0}
                                                                            : std::nullopt;
                    },
                    0.0);
            }
        }
    }
    return out;
}

// Whether two nodes say different things, by the differ's measure, with what
// this output places -- the code of a declaration or a synopsis -- left out.
bool wording_differs(ir::Node draft, ir::Node paper) {
    const auto strip = [](ir::Node& node) {
        if (auto* item = std::get_if<ir::SpecItem>(&node))
            item->decl = {};
        else if (auto* synopsis = std::get_if<ir::Synopsis>(&node))
            synopsis->code = {};
    };
    strip(draft);
    strip(paper);
    ir::Document a;
    ir::Document b;
    a.nodes.push_back(ir::Section{"x", "x", {std::move(draft)}});
    b.nodes.push_back(ir::Section{"x", "x", {std::move(paper)}});
    return diff::has_marks(diff::compare(a, b, {.context = diff::Context::All}));
}

void place_nodes(const std::vector<ir::Node>& draft,
                 const std::vector<ir::Node>& paper,
                 const std::string&           where,
                 const ir::NodeOrigin&        parent,
                 Collector&                   out);

// The lines of the draft's source that a declaration was read from, whole:
// from the start of its first line through the line break after its last.
std::optional<ir::SourceRange> declaration_lines(const ir::CodeText& code, Collector& out) {
    const ir::CodeSource* source = code.origin.source.get();
    if (source == nullptr || source->bytes.empty())
        return std::nullopt;
    const auto text = out.text_of(source->file);
    if (!text)
        return std::nullopt;
    const std::size_t first = source->bytes.front().begin;
    const std::size_t last  = source->bytes.back().end;
    const std::size_t begin = first == 0 ? 0 : text->rfind('\n', first - 1) + 1;
    const std::size_t end   = std::min(text->find('\n', last), text->size());
    return ir::SourceRange{source->file, begin, end < text->size() ? end + 1 : end};
}

// A declaration the paper adds to an item, as lines of the draft's
// `itemdecl` after the declaration it follows, or before the first when it
// follows none (issue #346).
bool place_declaration(const ir::CodeText& signature,
                       const ir::CodeText* after,
                       const ir::CodeText* before,
                       Collector&          out) {
    const ir::CodeText               paper = as_imported(signature);
    const std::optional<std::string> text  = listing_text(paper, 0, paper.text.size(), false);
    const auto                       lines = after != nullptr    ? declaration_lines(*after, out)
                                             : before != nullptr ? declaration_lines(*before, out)
                                                                 : std::nullopt;
    if (!text || !lines)
        return false;
    const std::size_t at = after != nullptr ? lines->end : lines->begin;
    out.edits[lines->file].push_back({at, at, *text + '\n'});
    return true;
}

void place_item(const ir::SpecItem& draft, const ir::SpecItem& paper, const std::string& where, Collector& out) {
    const std::string here =
        paper.decl.signatures.empty() ? where : std::format("{}: `{}`", where, paper.decl.signatures.front().text);
    // Paper and draft signatures paired most alike first, so a declaration the
    // paper adds cannot take the draft one that another reads more like.
    std::vector<bool>                       claimed(draft.decl.signatures.size(), false);
    std::vector<std::optional<std::size_t>> claims(paper.decl.signatures.size());
    struct Candidate {
        double      score;
        std::size_t p;
        std::size_t d;
    };
    std::vector<Candidate> candidates =
        std::views::cartesian_product(std::views::iota(std::size_t{0}, paper.decl.signatures.size()),
                                      std::views::iota(std::size_t{0}, draft.decl.signatures.size())) |
        std::views::transform([&](const auto& pd) {
            const auto [p, d] = pd;
            return Candidate{alike(draft.decl.signatures[d], paper.decl.signatures[p]), p, d};
        }) |
        std::ranges::to<std::vector>();
    std::ranges::stable_sort(candidates, std::ranges::greater{}, &Candidate::score);
    std::ranges::for_each(candidates, [&](const Candidate& c) {
        if (claims[c.p] || claimed[c.d])
            return;
        claims[c.p]  = c.d;
        claimed[c.d] = true;
    });
    // substrate generic algorithm: each paired signature, its code placed.
    for (std::size_t p = 0; p < claims.size(); ++p)
        if (claims[p])
            place_code(draft.decl.signatures[*claims[p]], paper.decl.signatures[p], here, out);
    // A declaration the paper adds goes after the draft's declaration that the
    // one before it claimed, or before the first one claimed.
    // substrate generic algorithm: each added declaration, placed beside its neighbour.
    for (std::size_t p = 0; p < claims.size(); ++p) {
        if (claims[p])
            continue;
        const auto          earlier = std::ranges::find_if(claims | std::views::take(p) | std::views::reverse,
                                                           [](const auto& claim) { return claim.has_value(); });
        const auto          later   = std::ranges::find_if(claims | std::views::drop(p + 1),
                                                           [](const auto& claim) { return claim.has_value(); });
        const ir::CodeText* after   = earlier != (claims | std::views::take(p) | std::views::reverse).end()
                                          ? &draft.decl.signatures[**earlier]
                                          : nullptr;
        const ir::CodeText* before =
            later != (claims | std::views::drop(p + 1)).end() ? &draft.decl.signatures[**later] : nullptr;
        if (!place_declaration(paper.decl.signatures[p], after, before, out))
            out.error(here, "the paper adds a declaration to this item, which cannot be placed in the draft's source");
    }
    // A declaration the paper removes goes with its lines.
    // substrate generic algorithm: each draft declaration no paper one claimed.
    for (std::size_t d = 0; d < claimed.size(); ++d) {
        if (claimed[d])
            continue;
        if (const auto lines = declaration_lines(draft.decl.signatures[d], out))
            out.edits[lines->file].push_back({lines->begin, lines->end, {}});
        else
            out.error(here,
                      "the paper removes a declaration from this item, which cannot be placed in the draft's source");
    }
    if (wording_differs(draft, paper))
        place_description(draft, paper, here, out);
}

void place_node(const ir::Node& draft, const ir::Node& paper, const std::string& where, Collector& out) {
    if (const auto* section = std::get_if<ir::Section>(&paper)) {
        const auto& draft_section = std::get<ir::Section>(draft);
        place_nodes(draft_section.children, section->children, section->stable_name, draft_section.origin, out);
        return;
    }
    if (const auto* synopsis = std::get_if<ir::Synopsis>(&paper)) {
        place_code(
            std::get<ir::Synopsis>(draft).code, synopsis->code, std::format("{}: {}", where, describe(paper)), out);
        return;
    }
    if (const auto* item = std::get_if<ir::SpecItem>(&paper)) {
        place_item(std::get<ir::SpecItem>(draft), *item, where, out);
        return;
    }
    if (wording_differs(draft, paper))
        out.error(where, "the paper changes a paragraph, which cannot be placed in the draft's source yet");
}

void place_nodes(const std::vector<ir::Node>& draft,
                 const std::vector<ir::Node>& paper,
                 const std::string&           where,
                 const ir::NodeOrigin&        parent,
                 Collector&                   out) {
    const std::vector<std::optional<std::size_t>> pairs = pair_nodes(draft, paper);
    std::vector<bool>                             used(draft.size(), false);
    // The level a subclause the paper adds here takes: a subclause beside it's,
    // or one below the subclause these nodes are in.
    const auto sibling =
        std::ranges::find_if(draft, [](const ir::Node& node) { return std::holds_alternative<ir::Section>(node); });
    const std::optional<int> level = sibling != draft.end()
                                         ? rsec_level(origin_of(*sibling), out)
                                         : rsec_level(parent, out).transform([](int above) { return above + 1; });
    // Where a node the paper adds goes: after the draft's node the paper's
    // node before it pairs with, or, for one ahead of every pair, before the
    // first paired node.
    const auto anchor_for = [&](std::size_t p) -> std::pair<ir::NodeOrigin, bool> {
        // substrate generic algorithm: a search back for the nearest pair.
        for (std::size_t q = p; q-- > 0;)
            if (pairs[q])
                return {origin_of(draft[*pairs[q]]), false};
        // substrate generic algorithm: a search forward for the nearest pair.
        for (std::size_t q = p + 1; q < paper.size(); ++q)
            if (pairs[q])
                return {origin_of(draft[*pairs[q]]), true};
        return {};
    };
    // substrate generic algorithm: each paper node, placed against its pair.
    for (std::size_t p = 0; p < paper.size(); ++p) {
        if (!pairs[p]) {
            const auto [anchor, before] = anchor_for(p);
            if (!place_added(paper[p], anchor, before, level, out))
                out.error(where,
                          std::format("the paper adds {}, which cannot be placed in the draft's source yet",
                                      describe(paper[p])));
            continue;
        }
        used[*pairs[p]] = true;
        place_node(draft[*pairs[p]], paper[p], where, out);
    }
    // substrate generic algorithm: what the paper does not have of the draft's.
    for (std::size_t d = 0; d < draft.size(); ++d)
        if (!used[d] && !place_removed(origin_of(draft[d]), out))
            out.error(where,
                      std::format("the paper removes {}, which cannot be placed in the draft's source yet",
                                  describe(draft[d])));
}

Plan finish(std::map<std::string, std::vector<Edit>> edits, std::vector<Finding> findings) {
    Plan plan;
    plan.findings = std::move(findings);
    // substrate generic algorithm: each file's edits, sorted and made distinct.
    for (auto& [file, list] : edits) {
        std::ranges::sort(list, {}, [](const Edit& e) { return std::pair{e.begin, e.end}; });
        list.erase(std::ranges::unique(list).begin(), list.end());
        plan.files.push_back({file, std::move(list)});
    }
    return plan;
}

} // namespace

Plan plan(const ir::Document& draft, const ir::Document& paper, Reader read) {
    Collector out;
    out.read = std::move(read);
    // The draft is subclauses, so only the paper's subclauses have a place in
    // it. Anything the paper has outside every one is left out, and said so.
    std::vector<ir::Node> sections;
    // substrate generic algorithm: a partition that reports one side.
    for (const ir::Node& node : paper.nodes) {
        if (std::holds_alternative<ir::Section>(node))
            sections.push_back(node);
        else
            out.findings.push_back(
                {Severity::Warning,
                 describe(node),
                 "it is in no subclause, so it has no place in the draft's source and is left out"});
    }
    place_nodes(draft.nodes, sections, {}, ir::NodeOrigin{}, out);
    return finish(std::move(out.edits), std::move(out.findings));
}

Plan merge(std::vector<Plan> plans) {
    std::map<std::string, std::vector<Edit>> edits;
    std::vector<Finding>                     findings;
    // substrate generic algorithm: gathers every plan's edits by file.
    for (Plan& one : plans) {
        findings.append_range(std::move(one.findings));
        // substrate generic algorithm: the same gathering, one plan's files.
        for (FileEdits& file : one.files)
            edits[file.file].append_range(std::move(file.edits));
    }
    return finish(std::move(edits), std::move(findings));
}

std::expected<std::string, std::string> apply(std::string_view text, std::span<const Edit> edits) {
    std::vector<Edit> sorted(edits.begin(), edits.end());
    std::ranges::stable_sort(sorted, {}, [](const Edit& e) { return std::pair{e.begin, e.end}; });
    std::string out;
    std::size_t pos = 0;
    // substrate generic algorithm: copies the text between the edits, each in place.
    for (const Edit& edit : sorted) {
        if (edit.begin < pos || edit.end < edit.begin || edit.end > text.size())
            return std::unexpected(
                std::format("an edit of bytes {} to {} overlaps another or runs past the end", edit.begin, edit.end));
        out += text.substr(pos, edit.begin - pos);
        out += edit.text;
        pos = edit.end;
    }
    out += text.substr(pos);
    return out;
}

} // namespace beman::specgen::backend::draft_edit
