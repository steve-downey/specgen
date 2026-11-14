// src/beman/specgen/draft_import.cpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/draft_import.hpp>

#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/foundation/overloaded.hpp>
#include <beman/specgen/ir.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace beman::specgen::draft_import {

namespace {

using beman::specgen::foundation::overloaded;
using Lines = std::span<const std::string>;

// --- text helpers ------------------------------------------------------------

std::string_view trim(std::string_view s) {
    const auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };
    const auto first     = std::ranges::find_if(s, not_space);
    if (first == s.end())
        return {};
    const auto last = std::ranges::find_if(s | std::views::reverse, not_space);
    return std::string_view(first, last.base());
}

bool is_letter(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }

std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines;
    // substrate generic algorithm: a split that keeps the empty lines, which
    // std::views::split on a string_view does, but yields subranges that must
    // be rebuilt as strings one at a time anyway.
    std::size_t begin = 0;
    // substrate generic algorithm: a cursor scan over variable-width input; the position is the loop's state.
    while (begin <= text.size()) {
        const std::size_t end = text.find('\n', begin);
        if (end == std::string_view::npos) {
            if (begin < text.size())
                lines.emplace_back(text.substr(begin));
            break;
        }
        lines.emplace_back(text.substr(begin, end - begin));
        begin = end + 1;
    }
    return lines;
}

// A prose line with its LaTeX comment removed. A `%` that is not escaped ends
// the line; when it is the last thing on it, the line break is not a space.
struct ProseLine {
    std::string_view text;
    bool             joins_next = false;
};

ProseLine strip_comment(std::string_view line) {
    // substrate generic algorithm: the first unescaped `%` is a scan with
    // look-behind, which no ranges algorithm expresses without an index.
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\') {
            ++i;
            continue;
        }
        if (line[i] == '%') {
            const std::string_view before = trim(line.substr(0, i));
            return {before, true};
        }
    }
    return {trim(line), false};
}

// Prose lines joined into one string: a line break is a space unless the
// line ended in `%`.
std::string join_prose(Lines lines) {
    std::string out;
    bool        joined = true;
    // substrate generic algorithm: a cursor scan over variable-width input; the position is the loop's state.
    for (const std::string& raw : lines) {
        const ProseLine line = strip_comment(raw);
        if (line.text.empty()) {
            joined = joined || line.joins_next;
            continue;
        }
        if (!joined && !out.empty())
            out += ' ';
        out += line.text;
        joined = line.joins_next;
    }
    return out;
}

bool starts_with_command(std::string_view line, std::string_view command) {
    const std::string_view t = trim(line);
    if (!t.starts_with(command))
        return false;
    return t.size() == command.size() || !is_letter(t[command.size()]);
}

// The `{...}` group at `pos`, balanced; an escaped brace does not count.
struct Braced {
    std::string inner;
    std::size_t next = 0;
};

std::optional<Braced> read_braced(std::string_view s, std::size_t pos, char open = '{', char close = '}') {
    if (pos >= s.size() || s[pos] != open)
        return std::nullopt;
    int         depth = 0;
    std::size_t i     = pos;
    // substrate generic algorithm: a balanced-delimiter scan with an escape
    // look-behind.
    for (; i < s.size(); ++i) {
        if (s[i] == '\\') {
            ++i;
            continue;
        }
        if (s[i] == open)
            ++depth;
        else if (s[i] == close && --depth == 0)
            return Braced{std::string(s.substr(pos + 1, i - pos - 1)), i + 1};
    }
    return std::nullopt;
}

// --- the importer -------------------------------------------------------------

struct RSecHeader {
    int         level = 0;
    std::string stable;
    std::string title;
};

struct Importer {
    std::vector<Diagnostic> diagnostics;
    std::set<std::string>   reported;
    std::string             section;

    void report(Severity severity, std::string message) {
        if (reported.insert(std::format("{}|{}|{}", static_cast<int>(severity), section, message)).second)
            diagnostics.push_back({severity, section, std::move(message)});
    }

    Result run(std::string_view tex, const Options& options);

    ir::CodeText              parse_code(std::string_view raw, bool listing);
    ir::Paragraph             parse_prose(std::string_view text, bool in_table = false);
    ir::InformativeBody       parse_informative(Lines lines);
    ir::ItemDescr             parse_description(Lines lines);
    std::vector<ir::Node>     parse_region(Lines lines);
    ir::Section               parse_section(Lines lines, std::size_t header, std::size_t end);
    std::vector<ir::CodeText> split_signatures(Lines lines);
    ir::Table2D               make_table2d(const struct EvTable& table);
    ir::Table1D               make_table1d(const struct EvTable& table);
};

// The names an item declares, which the differ pairs items by. The generator
// takes them from the declarations; the draft says them in its `\indexlibrary`
// entries, which name the same thing -- a constructor by its class, a
// destructor with the tilde the declaration has. A zombie name and a
// miscellaneous entry are not declarations.
std::vector<std::string> declared_entities(const std::vector<ir::IndexEntry>& index) {
    std::vector<std::string> names;
    // substrate generic algorithm: collects the distinct names in order of first appearance.
    for (const ir::IndexEntry& entry : index) {
        if (entry.kind == ir::IndexKind::Zombie || entry.kind == ir::IndexKind::Misc)
            continue;
        std::string name = entry.kind == ir::IndexKind::Destructor ? "~" + entry.name : entry.name;
        if (std::ranges::find(names, name) == names.end())
            names.push_back(std::move(name));
    }
    return names;
}

// --- code ---------------------------------------------------------------------

struct CodeBuilder {
    ir::CodeText code;

    void text(std::string_view s) { code.text += s; }
    void span(ir::SpanKind kind, std::string payload, std::string_view spelling) {
        const std::size_t begin = code.text.size();
        code.text += spelling;
        code.spans.push_back({begin, code.text.size(), kind, std::move(payload)});
    }
};

// One draft macro with a code meaning, at `pos` (a backslash). Appends its
// span to `out` and returns where it ended, or nullopt when it is not one.
std::optional<std::size_t> code_macro(std::string_view s, std::size_t pos, CodeBuilder& out) {
    std::size_t end = pos + 1;
    // substrate generic algorithm: a cursor scan over variable-width input; the position is the loop's state.
    while (end < s.size() && is_letter(s[end]))
        ++end;
    const std::string_view name = s.substr(pos + 1, end - pos - 1);

    const auto one_arg = [&](ir::SpanKind kind, bool spelled_as_payload) -> std::optional<std::size_t> {
        const auto arg = read_braced(s, end);
        if (!arg)
            return std::nullopt;
        out.span(kind, arg->inner, spelled_as_payload ? arg->inner : "[" + arg->inner + "]");
        return arg->next;
    };

    if (name == "exposid" || name == "exposidnc")
        return one_arg(ir::SpanKind::ExposId, true);
    if (name == "placeholder" || name == "placeholdernc")
        return one_arg(ir::SpanKind::Placeholder, true);
    if (name == "ref")
        return one_arg(ir::SpanKind::Ref, false);
    if (name == "cv") {
        out.span(ir::SpanKind::Placeholder, "cv", "cv");
        const auto empty = read_braced(s, end);
        return empty ? empty->next : end;
    }
    if (name == "seebelow") {
        out.span(ir::SpanKind::SeeBelow, "", "SEEBELOW");
        return end;
    }
    if (name == "impdef") {
        out.span(ir::SpanKind::ImplDefined, "", "implementation-defined");
        return end;
    }
    if (name == "libglobal") {
        const auto arg = read_braced(s, end);
        if (!arg)
            return std::nullopt;
        out.span(ir::SpanKind::LibraryIndex, "", arg->inner);
        return arg->next;
    }
    if (name == "libmember") {
        const auto name_arg = read_braced(s, end);
        const auto cls_arg  = name_arg ? read_braced(s, name_arg->next) : std::nullopt;
        if (!cls_arg)
            return std::nullopt;
        out.span(ir::SpanKind::LibraryIndex, cls_arg->inner, name_arg->inner);
        return cls_arg->next;
    }
    return std::nullopt;
}

// The text-mode escapes `\tcode{}` takes for characters LaTeX reserves.
std::optional<std::string_view> text_escape(std::string_view s, std::size_t pos, std::size_t& next) {
    constexpr std::pair<std::string_view, std::string_view> table[]{{"\\&", "&"},
                                                                    {"\\#", "#"},
                                                                    {"\\~", "~"},
                                                                    {"\\%", "%"},
                                                                    {"\\{", "{"},
                                                                    {"\\}", "}"},
                                                                    {"\\$", "$"},
                                                                    {"\\_", "_"},
                                                                    {"\\textasciitilde{}", "~"},
                                                                    {"\\textasciicircum{}", "^"},
                                                                    {"\\textbackslash{}", "\\"}};
    const auto it = std::ranges::find_if(table, [&](const auto& e) { return s.substr(pos).starts_with(e.first); });
    if (it == std::end(table))
        return std::nullopt;
    next = pos + it->first.size();
    return it->second;
}

ir::CodeText Importer::parse_code(std::string_view raw, bool listing) {
    CodeBuilder out;
    std::size_t i = 0;
    // substrate generic algorithm: a single left-to-right scan that consumes
    // a variable-length macro or escape at each step; the position is the
    // loop's state.
    while (i < raw.size()) {
        const char c = raw[i];
        if (listing && c == '@') {
            const std::size_t close = raw.find('@', i + 1);
            if (close == std::string_view::npos) {
                out.text(raw.substr(i));
                break;
            }
            const std::string_view inner = raw.substr(i + 1, close - i - 1);
            // The escape is spans when every macro in it is one; otherwise it
            // stays the author's own escape, as in a \verbatim-itemdecl.
            CodeBuilder trial;
            std::size_t j  = 0;
            bool        ok = !inner.empty();
            // substrate generic algorithm: a cursor scan over variable-width input; the position is the loop's state.
            while (ok && j < inner.size()) {
                if (inner[j] != '\\') {
                    ok = false;
                    break;
                }
                const auto next = code_macro(inner, j, trial);
                if (!next) {
                    ok = false;
                    break;
                }
                j = *next;
            }
            if (ok) {
                const std::size_t shift = out.code.text.size();
                out.code.text += trial.code.text;
                // substrate generic algorithm: a cursor scan over variable-width input; the position is the loop's
                // state.
                for (ir::Span span : trial.code.spans) {
                    span.begin += shift;
                    span.end += shift;
                    out.code.spans.push_back(std::move(span));
                }
            } else {
                out.text(raw.substr(i, close - i + 1));
            }
            i = close + 1;
            continue;
        }
        if (c == '\\') {
            if (listing) {
                // Inside a listing only the comment macros are TeX (texcl).
                if (raw.substr(i).starts_with("\\ref{")) {
                    if (const auto next = code_macro(raw, i, out)) {
                        i = *next;
                        continue;
                    }
                }
                out.text(raw.substr(i, 1));
                ++i;
                continue;
            }
            std::size_t next = 0;
            if (const auto escaped = text_escape(raw, i, next)) {
                out.text(*escaped);
                i = next;
                continue;
            }
            if (const auto after = code_macro(raw, i, out)) {
                i = *after;
                continue;
            }
            if (raw.substr(i).starts_with("\\brk") || raw.substr(i).starts_with("\\-")) {
                i += raw.substr(i).starts_with("\\-") ? 2 : 4;
                if (raw.substr(i).starts_with("{}"))
                    i += 2;
                continue;
            }
            if (raw.substr(i).starts_with("\\ ") || raw.substr(i).starts_with("\\,")) {
                out.text(raw.substr(i, 2) == "\\ " ? " " : "");
                i += 2;
                continue;
            }
            report(Severity::Warning, "a macro in \\tcode is kept as written: " + std::string(raw.substr(i, 12)));
        }
        out.text(raw.substr(i, 1));
        ++i;
    }
    return std::move(out.code);
}

// --- prose --------------------------------------------------------------------

ir::CodeText whole_span(ir::SpanKind kind, std::string payload, std::string spelling) {
    ir::CodeText code{std::move(spelling), {}};
    code.spans.push_back({0, code.text.size(), kind, std::move(payload)});
    return code;
}

ir::Inline code_inline_of(ir::CodeText code) { return ir::CodeInline{std::move(code)}; }

ir::Paragraph Importer::parse_prose(std::string_view text, bool in_table) {
    ir::Paragraph out;
    std::string   pending;
    const auto    flush = [&] {
        if (!pending.empty())
            out.push_back(ir::TextInline{std::exchange(pending, {})});
    };
    const auto raw = [&](std::string_view latex, std::string why) {
        flush();
        report(Severity::Warning, std::move(why));
        out.push_back(ir::RawInline{std::string(latex)});
    };

    std::size_t i = 0;
    // substrate generic algorithm: a left-to-right scan that consumes a
    // variable-length macro at each step.
    while (i < text.size()) {
        const char c = text[i];
        if (c == '$') {
            const std::size_t close = text.find('$', i + 1);
            const std::size_t end   = close == std::string_view::npos ? text.size() : close + 1;
            raw(text.substr(i, end - i), "mathematics is kept as raw LaTeX");
            i = end;
            continue;
        }
        if (c != '\\') {
            pending += c;
            ++i;
            continue;
        }
        if (i + 1 >= text.size()) {
            pending += c;
            break;
        }
        if (!is_letter(text[i + 1])) {
            const char next = text[i + 1];
            if (next == ' ') {
                pending += ' ';
            } else if (in_table && next == '&') {
                pending += '&'; // the LaTeX backend escapes a table cell's `&` again
            } else if (next != '-' && next != ',') {
                pending += text.substr(i, 2); // \& \% \{ ... stay as the draft writes them
            }
            i += 2;
            continue;
        }
        std::size_t end = i + 1;
        // substrate generic algorithm: a cursor scan over variable-width input; the position is the loop's state.
        while (end < text.size() && is_letter(text[end]))
            ++end;
        const std::string name(text.substr(i + 1, end - i - 1));
        const auto        arg = read_braced(text, end);

        if (name == "tcode" && arg) {
            flush();
            out.push_back(code_inline_of(parse_code(arg->inner, false)));
            i = arg->next;
        } else if ((name == "exposid" || name == "exposidnc") && arg) {
            flush();
            out.push_back(code_inline_of(whole_span(ir::SpanKind::ExposId, arg->inner, arg->inner)));
            i = arg->next;
        } else if (name == "seebelow" || name == "impdef") {
            flush();
            out.push_back(code_inline_of(name == "seebelow"
                                             ? whole_span(ir::SpanKind::SeeBelow, "", "SEEBELOW")
                                             : whole_span(ir::SpanKind::ImplDefined, "", "implementation-defined")));
            i = end;
        } else if ((name == "placeholder" || name == "placeholdernc") && arg) {
            flush();
            out.push_back(ir::PlaceholderInline{arg->inner});
            i = arg->next;
        } else if (name == "cv") {
            flush();
            out.push_back(ir::PlaceholderInline{"cv"});
            i = arg ? arg->next : end;
        } else if (name == "oldconcept" && arg) {
            flush();
            out.push_back(ir::OldConceptRef{arg->inner});
            i = arg->next;
        } else if (name == "libconcept" && arg) {
            flush();
            out.push_back(ir::ConceptRef{arg->inner});
            i = arg->next;
        } else if (name == "tref" && arg) {
            flush();
            out.push_back(ir::TableRef{arg->inner});
            i = arg->next;
        } else if (name == "iref" && arg) {
            flush();
            out.push_back(ir::RefInline{arg->inner});
            i = arg->next;
        } else if (name == "brk" || name == "br" || name == "linebreak" || name == "newline" ||
                   name == "nolinebreak") {
            i = arg && name != "brk" && name != "br" && name != "newline" ? arg->next : end;
        } else {
            // Any other macro, with the groups that follow it, as written.
            std::size_t stop = end;
            // substrate generic algorithm: a cursor scan over variable-width input; the position is the loop's state.
            while (const auto group = read_braced(text, stop))
                stop = group->next;
            raw(text.substr(i, stop - i), "\\" + name + " has no IR kind and is kept as raw LaTeX");
            i = stop;
        }
    }
    flush();
    return out;
}

// --- blocks -------------------------------------------------------------------

struct EvPnum {
    std::optional<ir::ElementKind> tag;
};
struct EvProse {
    std::string text;
};
struct EvItemize {
    std::vector<std::string> items;
};
struct EvCode {
    std::string code;
};
struct EvNote {
    bool                     example;
    std::vector<std::string> inner;
};
struct EvTable {
    bool                                  flat = false; // libtab2; else lib2dtab2
    std::string                           caption;
    std::string                           stable;
    std::string                           column1;
    std::string                           column2;
    std::vector<std::vector<std::string>> rows; // [header,] cell1, cell2
};
struct EvRaw {
    std::string latex;
    std::string environment;
};
using Event = std::variant<EvPnum, EvProse, EvItemize, EvCode, EvNote, EvTable, EvRaw>;

std::optional<std::string> begin_name(std::string_view line) {
    const std::string_view t = trim(line);
    if (!t.starts_with("\\begin{"))
        return std::nullopt;
    const auto name = read_braced(t, 6);
    if (!name)
        return std::nullopt;
    return name->inner;
}

// The index of the line closing the environment `name` opened at `open`.
std::size_t environment_end(Lines lines, std::size_t open, const std::string& name) {
    int depth = 0;
    // substrate generic algorithm: a cursor scan over variable-width input; the position is the loop's state.
    for (std::size_t i = open; i < lines.size(); ++i) {
        const std::string_view t = trim(lines[i]);
        if (t.starts_with("\\begin{" + name + "}"))
            ++depth;
        else if (t.starts_with("\\end{" + name + "}") && --depth == 0)
            return i;
    }
    return lines.size();
}

// What follows `\begin{name}` on its own line (an optional argument group
// and then text), or nothing.
std::string after_begin(std::string_view line, const std::string& name) {
    return std::string(trim(trim(line).substr(std::string("\\begin{").size() + name.size() + 1)));
}

std::optional<ir::ElementKind> element_tag(std::string_view line, std::string& rest) {
    const std::string_view t = trim(line);
    if (t.size() < 2 || t.front() != '\\')
        return std::nullopt;
    std::size_t end = 1;
    // substrate generic algorithm: a cursor scan over variable-width input; the position is the loop's state.
    while (end < t.size() && is_letter(t[end]))
        ++end;
    const auto kind = ir::element_from_name(t.substr(1, end - 1));
    if (!kind)
        return std::nullopt;
    rest = std::string(trim(t.substr(end)));
    return kind;
}

std::optional<RSecHeader> parse_rsec(std::string_view line) {
    const std::string_view t = trim(line);
    if (!t.starts_with("\\rSec") || t.size() < 8 || std::isdigit(static_cast<unsigned char>(t[5])) == 0)
        return std::nullopt;
    const auto stable = read_braced(t, 6, '[', ']');
    const auto title  = stable ? read_braced(t, stable->next) : std::nullopt;
    if (!title)
        return std::nullopt;
    return RSecHeader{t[5] - '0', stable->inner, title->inner};
}

std::optional<ir::IndexEntry> parse_index(std::string_view line) {
    constexpr std::pair<std::string_view, std::pair<ir::IndexKind, int>> table[]{
        {"\\indexlibraryglobal", {ir::IndexKind::Global, 1}},
        {"\\indexlibraryctor", {ir::IndexKind::Constructor, 1}},
        {"\\indexlibrarydtor", {ir::IndexKind::Destructor, 1}},
        {"\\indexlibrarymemberexpos", {ir::IndexKind::MemberExpos, 2}},
        {"\\indexlibrarymemberx", {ir::IndexKind::MemberX, 2}},
        {"\\indexlibrarymember", {ir::IndexKind::Member, 2}},
        {"\\indexlibraryzombie", {ir::IndexKind::Zombie, 1}},
        {"\\indexlibrarymisc", {ir::IndexKind::Misc, 2}}};
    const std::string_view t = trim(line);
    // substrate generic algorithm: a cursor scan over variable-width input; the position is the loop's state.
    for (const auto& [prefix, shape] : table) {
        if (!t.starts_with(prefix) || (t.size() > prefix.size() && is_letter(t[prefix.size()])))
            continue;
        const auto first  = read_braced(t, prefix.size());
        const auto second = shape.second == 2 && first ? read_braced(t, first->next) : std::nullopt;
        if (!first || (shape.second == 2 && !second))
            return std::nullopt;
        return ir::IndexEntry{shape.first, first->inner, second ? second->inner : std::string{}};
    }
    return std::nullopt;
}

// The cells of one table row, split at the `&` that is not escaped and not
// inside braces.
std::vector<std::string> split_cells(std::string_view row) {
    std::vector<std::string> cells;
    std::string              current;
    int                      depth = 0;
    // substrate generic algorithm: a split on a delimiter that is only a
    // delimiter outside braces and when not escaped, so it carries state.
    for (std::size_t i = 0; i < row.size(); ++i) {
        const char c = row[i];
        if (c == '\\' && i + 1 < row.size()) {
            current += row.substr(i, 2);
            ++i;
            continue;
        }
        depth += c == '{' ? 1 : c == '}' ? -1 : 0;
        if (c == '&' && depth == 0) {
            cells.push_back(std::string(trim(current)));
            current.clear();
        } else {
            current += c;
        }
    }
    cells.push_back(std::string(trim(current)));
    return cells;
}

// `\begin{lib2dtab2}{caption}{stable}{col1}{col2}` and
// `\begin{libtab2}{caption}{stable}{colspec}{col1}{col2}`, each followed by
// rows of `&`-separated cells ending `\\`, between `\rowsep`s. A lib2dtab2 row
// starts with `\rowhdr{...}`. Nothing else is read as a table.
std::optional<EvTable> parse_table(bool flat, std::string_view text) {
    EvTable table;
    table.flat                   = flat;
    std::size_t              pos = 0;
    std::vector<std::string> groups;
    const std::size_t        wanted = flat ? 5 : 4;
    // substrate generic algorithm: the header groups are read one after the
    // other, each from where the last ended.
    while (groups.size() < wanted) {
        // substrate generic algorithm: skipping the whitespace between groups.
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0)
            ++pos;
        const auto group = read_braced(text, pos);
        if (!group)
            return std::nullopt;
        groups.push_back(group->inner);
        pos = group->next;
    }
    table.caption = groups[0];
    table.stable  = groups[1];
    table.column1 = groups[flat ? 3 : 2];
    table.column2 = groups[flat ? 4 : 3];

    std::string_view rest = text.substr(pos);
    // substrate generic algorithm: rows are separated by a literal `\rowsep`,
    // found one after another.
    while (!trim(rest).empty()) {
        const std::size_t sep   = rest.find("\\rowsep");
        std::string_view  piece = trim(rest.substr(0, sep));
        rest                    = sep == std::string_view::npos ? std::string_view{} : rest.substr(sep + 7);
        if (piece.empty())
            continue;
        if (piece.ends_with("\\\\"))
            piece = trim(piece.substr(0, piece.size() - 2));
        std::vector<std::string> cells = split_cells(piece);
        if (!flat) {
            const std::string_view first = trim(cells.front());
            const auto             hdr   = first.starts_with("\\rowhdr") ? read_braced(first, 7) : std::nullopt;
            if (!hdr || cells.size() != 3)
                return std::nullopt;
            cells.front() = hdr->inner;
        } else if (cells.size() != 2) {
            return std::nullopt;
        }
        table.rows.push_back(std::move(cells));
    }
    return table;
}

// --- scanning a body into events -------------------------------------------------

void scan_events(Importer& imp, Lines lines, std::vector<Event>& events) {
    std::vector<std::string> prose;
    const auto               flush = [&] {
        const std::string text = join_prose(prose);
        prose.clear();
        if (!text.empty())
            events.push_back(EvProse{text});
    };

    std::size_t i = 0;
    // substrate generic algorithm: the scan consumes a variable number of
    // lines per step (an environment, a tag line), so the index is its state.
    while (i < lines.size()) {
        const std::string_view line = lines[i];
        const std::string_view t    = trim(line);
        if (t.empty()) {
            flush();
            ++i;
            continue;
        }
        if (starts_with_command(t, "\\pnum")) {
            flush();
            EvPnum      pnum;
            std::string rest(trim(t.substr(5)));
            if (rest.empty()) {
                std::size_t j = i + 1;
                // substrate generic algorithm: a cursor scan over variable-width input; the position is the loop's
                // state.
                while (j < lines.size() && trim(lines[j]).empty())
                    ++j;
                std::string tag_rest;
                if (j < lines.size()) {
                    if (const auto tag = element_tag(lines[j], tag_rest)) {
                        pnum.tag = tag;
                        rest     = tag_rest;
                        i        = j;
                    }
                }
            } else if (const auto tag = element_tag(rest, rest)) {
                pnum.tag = tag;
            }
            events.push_back(pnum);
            if (!rest.empty())
                prose.push_back(rest);
            ++i;
            continue;
        }
        if (t.starts_with("\\index")) {
            ++i; // an index entry is not content
            continue;
        }
        if (const auto name = begin_name(t)) {
            flush();
            const std::size_t        end   = environment_end(lines, i, *name);
            const std::size_t        inner = std::min(end, lines.size());
            std::vector<std::string> body;
            if (const std::string rest = after_begin(t, *name); !rest.empty() && rest.front() != '{')
                body.push_back(rest);
            body.append_range(lines.subspan(i + 1, inner - i - 1));

            if (*name == "lib2dtab2" || *name == "libtab2") {
                std::vector<std::string> table_lines;
                table_lines.push_back(
                    std::string(trim(trim(t).substr(std::string("\\begin{").size() + name->size() + 1))));
                table_lines.append_range(lines.subspan(i + 1, inner - i - 1));
                if (auto table = parse_table(*name == "libtab2", join_prose(table_lines))) {
                    events.push_back(std::move(*table));
                } else {
                    imp.report(Severity::Warning, "a " + *name + " this importer cannot read is kept as raw LaTeX");
                    events.push_back(EvRaw{lines.subspan(i, std::min(end + 1, lines.size()) - i) |
                                               std::views::join_with('\n') | std::ranges::to<std::string>(),
                                           *name});
                }
            } else if (*name == "codeblock") {
                events.push_back(EvCode{body | std::views::join_with('\n') | std::ranges::to<std::string>()});
            } else if (*name == "note" || *name == "example") {
                events.push_back(EvNote{*name == "example", std::move(body)});
            } else if (*name == "itemize") {
                std::vector<std::string> items;
                std::vector<std::string> current;
                bool                     open   = false;
                bool                     nested = false;
                const auto               close  = [&] {
                    if (open)
                        items.push_back(join_prose(current));
                    current.clear();
                };
                // substrate generic algorithm: a cursor scan over variable-width input; the position is the loop's
                // state.
                for (const std::string& item_line : body) {
                    if (starts_with_command(item_line, "\\item")) {
                        close();
                        open = true;
                        current.push_back(std::string(trim(trim(item_line).substr(5))));
                    } else {
                        nested = nested || begin_name(item_line).has_value();
                        current.push_back(item_line);
                    }
                }
                close();
                if (nested) {
                    imp.report(Severity::Warning, "a nested list is kept as raw LaTeX");
                    events.push_back(EvRaw{lines.subspan(i, std::min(end + 1, lines.size()) - i) |
                                               std::views::join_with('\n') | std::ranges::to<std::string>(),
                                           *name});
                } else {
                    events.push_back(EvItemize{std::move(items)});
                }
            } else {
                imp.report(Severity::Warning, "the " + *name + " environment is kept as raw LaTeX");
                events.push_back(EvRaw{lines.subspan(i, std::min(end + 1, lines.size()) - i) |
                                           std::views::join_with('\n') | std::ranges::to<std::string>(),
                                       *name});
            }
            i = end + 1;
            continue;
        }
        prose.push_back(std::string(line));
        ++i;
    }
    flush();
}

// --- Importer ---------------------------------------------------------------------

ir::Table2D Importer::make_table2d(const EvTable& table) {
    ir::Table2D out{.stable_name = table.stable,
                    .caption     = parse_prose(table.caption, true),
                    .column1     = parse_prose(table.column1, true),
                    .column2     = parse_prose(table.column2, true),
                    .rows        = {}};
    out.rows =
        table.rows | std::views::transform([&](const std::vector<std::string>& row) {
            return ir::Table2DRow{parse_prose(row[0], true), parse_prose(row[1], true), parse_prose(row[2], true)};
        }) |
        std::ranges::to<std::vector>();
    return out;
}

ir::Table1D Importer::make_table1d(const EvTable& table) {
    ir::Table1D out{.stable_name = table.stable,
                    .caption     = parse_prose(table.caption, true),
                    .column1     = parse_prose(table.column1, true),
                    .column2     = parse_prose(table.column2, true),
                    .rows        = {}};
    out.rows = table.rows | std::views::transform([&](const std::vector<std::string>& row) {
                   return ir::Table1DRow{parse_prose(row[0], true), parse_prose(row[1], true)};
               }) |
               std::ranges::to<std::vector>();
    return out;
}

ir::InformativeBody Importer::parse_informative(Lines lines) {
    std::vector<Event> events;
    scan_events(*this, lines, events);
    ir::InformativeBody body;
    // substrate generic algorithm: a cursor scan over variable-width input; the position is the loop's state.
    for (const Event& event : events) {
        std::visit(
            overloaded{[&](const EvPnum&) {},
                       [&](const EvProse& e) { body.push_back(ir::BodyParagraph{parse_prose(e.text), false}); },
                       [&](const EvItemize& e) {
                           ir::Itemize list;
                           // substrate generic algorithm: a cursor scan over variable-width input; the position is the
                           // loop's state.
                           for (const std::string& item : e.items)
                               list.items.push_back(parse_prose(item));
                           body.push_back(std::move(list));
                       },
                       [&](const EvCode& e) { body.push_back(ir::AuthoredCode{parse_code(e.code, true)}); },
                       [&](const EvNote&) { report(Severity::Warning, "a note inside a note is dropped"); },
                       [&](const EvTable& e) {
                           if (e.flat)
                               body.push_back(make_table1d(e));
                           else
                               body.push_back(make_table2d(e));
                       },
                       [&](const EvRaw& e) { body.push_back(ir::BodyParagraph{{ir::RawInline{e.latex}}, false}); }},
            event);
    }
    return body;
}

ir::ItemDescr Importer::parse_description(Lines lines) {
    std::vector<Event> events;
    scan_events(*this, lines, events);
    ir::ItemDescr        descr;
    ir::DescriptionBody* target   = &descr.leading;
    bool                 new_para = false;
    bool                 fresh    = false; // a \pnum, and its tag if any, was the last thing read

    std::size_t k = 0;
    // substrate generic algorithm: "Equivalent to:" looks one event ahead and
    // consumes it, so the index is the loop's state.
    while (k < events.size()) {
        const Event& event     = events[k++];
        const bool   was_fresh = std::exchange(fresh, false);
        std::visit(
            overloaded{[&](const EvPnum& e) {
                           new_para = true;
                           fresh    = true;
                           if (e.tag) {
                               descr.elements.push_back(ir::DescriptionElement{.kind = *e.tag, .body = {}});
                               target = &descr.elements.back().body;
                           }
                       },
                       [&](const EvProse& e) {
                           if (was_fresh && e.text == "Equivalent to:" && k < events.size() &&
                               std::holds_alternative<EvCode>(events[k])) {
                               target->push_back(ir::EquivalentTo{parse_code(std::get<EvCode>(events[k]).code, true)});
                               ++k;
                               new_para = false;
                               return;
                           }
                           target->push_back(ir::BodyParagraph{parse_prose(e.text), !new_para});
                           new_para = false;
                       },
                       [&](const EvItemize& e) {
                           ir::Itemize list;
                           // substrate generic algorithm: a cursor scan over variable-width input; the position is the
                           // loop's state.
                           for (const std::string& item : e.items)
                               list.items.push_back(parse_prose(item));
                           target->push_back(std::move(list));
                       },
                       [&](const EvCode& e) { target->push_back(ir::AuthoredCode{parse_code(e.code, true)}); },
                       [&](const EvNote& e) {
                           if (e.example)
                               target->push_back(ir::Example{parse_informative(e.inner)});
                           else
                               target->push_back(ir::Note{parse_informative(e.inner)});
                       },
                       [&](const EvTable& e) {
                           if (e.flat)
                               target->push_back(make_table1d(e));
                           else
                               target->push_back(make_table2d(e));
                       },
                       [&](const EvRaw& e) { target->push_back(ir::BodyParagraph{{ir::RawInline{e.latex}}, true}); }},
            event);
    }
    return descr;
}

std::vector<ir::CodeText> Importer::split_signatures(Lines lines) {
    const std::string         text = lines | std::views::join_with('\n') | std::ranges::to<std::string>();
    std::vector<ir::CodeText> out;
    int                       depth = 0;
    std::size_t               start = 0;
    const auto                emit  = [&](std::size_t end) {
        const std::string_view sig = trim(std::string_view(text).substr(start, end - start));
        if (!sig.empty())
            out.push_back(parse_code(sig, true));
        start = end;
    };
    // substrate generic algorithm: a declaration ends at the first `;` outside
    // any bracket, so the scan carries the bracket depth; it cannot be split
    // by a delimiter alone.
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '{' || c == '(')
            ++depth;
        else if (c == '}' || c == ')')
            --depth;
        else if (c == ';' && depth <= 0) {
            std::size_t end = i + 1;
            // A trailing comment on the declaration's last line belongs to it.
            const std::size_t      eol  = std::min(text.find('\n', end), text.size());
            const std::string_view tail = trim(std::string_view(text).substr(end, eol - end));
            if (tail.starts_with("//"))
                end = eol;
            emit(end);
            i = end - 1;
        }
    }
    emit(text.size());
    return out;
}

// The class a synopsis declares, when it declares exactly one. `template<...>`
// parameter lists and `enum class` are skipped.
std::string declared_class(std::string_view code) {
    std::vector<std::string> names;
    std::size_t              i = 0;
    // substrate generic algorithm: a token scan that skips a bracketed
    // parameter list wholesale.
    while (i < code.size()) {
        if (!is_letter(code[i])) {
            ++i;
            continue;
        }
        std::size_t end = i;
        // substrate generic algorithm: a cursor scan over variable-width input; the position is the loop's state.
        while (end < code.size() &&
               (is_letter(code[end]) || std::isdigit(static_cast<unsigned char>(code[end])) != 0 || code[end] == '_'))
            ++end;
        const std::string_view word = code.substr(i, end - i);
        if (word == "template") {
            const std::size_t open  = code.find('<', end);
            std::size_t       j     = open;
            int               depth = 0;
            // substrate generic algorithm: a cursor scan over variable-width input; the position is the loop's state.
            while (open != std::string_view::npos && j < code.size()) {
                depth += code[j] == '<' ? 1 : code[j] == '>' ? -1 : 0;
                ++j;
                if (depth == 0)
                    break;
            }
            i = open == std::string_view::npos ? end : j;
            continue;
        }
        if ((word == "class" || word == "struct") && !trim(code.substr(0, i)).ends_with("enum")) {
            std::size_t name_begin = end;
            // substrate generic algorithm: a cursor scan over variable-width input; the position is the loop's state.
            while (name_begin < code.size() && std::isspace(static_cast<unsigned char>(code[name_begin])) != 0)
                ++name_begin;
            std::size_t name_end = name_begin;
            // substrate generic algorithm: a cursor scan over variable-width input; the position is the loop's state.
            while (name_end < code.size() &&
                   (is_letter(code[name_end]) || std::isdigit(static_cast<unsigned char>(code[name_end])) != 0 ||
                    code[name_end] == '_'))
                ++name_end;
            if (name_end > name_begin)
                names.emplace_back(code.substr(name_begin, name_end - name_begin));
            end = name_end;
        }
        i = end;
    }
    return names.size() == 1 ? names.front() : std::string{};
}

std::vector<ir::Node> Importer::parse_region(Lines lines) {
    std::vector<ir::Node>       nodes;
    std::vector<std::string>    loose;
    std::vector<ir::IndexEntry> pending;
    const auto                  has_content = [&] {
        return std::ranges::any_of(loose, [](const std::string& l) { return !trim(l).empty(); });
    };
    const auto flush_loose = [&] {
        if (has_content())
            nodes.push_back(ir::SpecItem{.decl = {}, .descr = parse_description(loose)});
        loose.clear();
    };

    std::size_t i = 0;
    // substrate generic algorithm: an item spans a variable number of lines
    // (its itemdecl, then its itemdescr), so the index is the loop's state.
    while (i < lines.size()) {
        const std::string_view t = trim(lines[i]);
        if (t.starts_with("\\indexlibrary")) {
            flush_loose();
            if (const auto entry = parse_index(t))
                pending.push_back(*entry);
            ++i;
            continue;
        }
        if (const auto name = begin_name(t)) {
            if (*name == "itemdecl") {
                flush_loose();
                const std::size_t end = environment_end(lines, i, *name);
                ir::SpecItem      item;
                item.decl.signatures = split_signatures(lines.subspan(i + 1, std::min(end, lines.size()) - i - 1));
                item.decl.index      = std::exchange(pending, {});
                item.decl.entities   = declared_entities(item.decl.index);
                std::size_t next     = end + 1;
                std::size_t probe    = next;
                // substrate generic algorithm: a cursor scan over variable-width input; the position is the loop's
                // state.
                while (probe < lines.size() && trim(lines[probe]).empty())
                    ++probe;
                if (probe < lines.size() && begin_name(lines[probe]) == std::optional<std::string>{"itemdescr"}) {
                    const std::size_t dend = environment_end(lines, probe, "itemdescr");
                    item.descr = parse_description(lines.subspan(probe + 1, std::min(dend, lines.size()) - probe - 1));
                    next       = dend + 1;
                }
                nodes.push_back(std::move(item));
                i = next;
                continue;
            }
            if (*name == "codeblock" && !has_content()) {
                const std::size_t end  = environment_end(lines, i, *name);
                const std::string code = lines.subspan(i + 1, std::min(end, lines.size()) - i - 1) |
                                         std::views::join_with('\n') | std::ranges::to<std::string>();
                ir::Synopsis      synopsis;
                synopsis.code = parse_code(code, true);
                synopsis.name = declared_class(synopsis.code.text);
                nodes.push_back(std::move(synopsis));
                pending.clear();
                i = end + 1;
                continue;
            }
        }
        loose.push_back(lines[i]);
        ++i;
    }
    flush_loose();
    return nodes;
}

ir::Section Importer::parse_section(Lines lines, std::size_t header, std::size_t end) {
    const RSecHeader  head  = *parse_rsec(lines[header]);
    const std::string saved = std::exchange(section, head.stable);
    ir::Section       out{.stable_name = head.stable, .title = head.title, .children = {}};

    std::vector<std::string> own;
    const auto               flush_own = [&] {
        out.children.append_range(parse_region(own));
        own.clear();
    };
    std::size_t i = header + 1;
    // substrate generic algorithm: a subsection consumes a variable number of
    // lines, so the index is the loop's state.
    while (i < end) {
        if (const auto sub = parse_rsec(lines[i]); sub && sub->level > head.level) {
            flush_own();
            std::size_t sub_end = i + 1;
            // substrate generic algorithm: a cursor scan over variable-width input; the position is the loop's state.
            while (sub_end < end) {
                const auto next = parse_rsec(lines[sub_end]);
                if (next && next->level <= sub->level)
                    break;
                ++sub_end;
            }
            out.children.push_back(parse_section(lines, i, sub_end));
            section = head.stable;
            i       = sub_end;
            continue;
        }
        own.push_back(lines[i]);
        ++i;
    }
    flush_own();
    section = saved;
    return out;
}

Result Importer::run(std::string_view tex, const Options& options) {
    const std::vector<std::string> lines = split_lines(tex);
    Result                         result;
    // substrate generic algorithm: a cursor scan over variable-width input; the position is the loop's state.
    for (const std::string& name : options.sections) {
        const auto header = std::ranges::find_if(lines, [&](const std::string& l) {
            const auto h = parse_rsec(l);
            return h && h->stable == name;
        });
        if (header == lines.end()) {
            diagnostics.push_back({Severity::Error, name, "the source has no subclause with this stable name"});
            continue;
        }
        const std::size_t begin = static_cast<std::size_t>(header - lines.begin());
        const int         level = parse_rsec(*header)->level;
        std::size_t       end   = begin + 1;
        // substrate generic algorithm: a cursor scan over variable-width input; the position is the loop's state.
        while (end < lines.size()) {
            const auto next = parse_rsec(lines[end]);
            if (next && next->level <= level)
                break;
            ++end;
        }
        result.document.nodes.push_back(parse_section(lines, begin, end));
    }
    result.diagnostics = std::move(diagnostics);
    return result;
}

} // namespace

Result import_sections(std::string_view tex, const Options& options) { return Importer{}.run(tex, options); }

namespace {

// Whether `tex` has a heading `\rSecN[name]` -- the bracket, the name, the
// bracket, on a line that holds `\rSec`. A name in running text does not count.
bool declares_section(std::string_view tex, std::string_view name) {
    const std::string needle = std::format("[{}]", name);
    std::size_t       at     = 0;
    // substrate generic algorithm: a search that resumes after each miss; the position is the loop's state.
    while ((at = tex.find(needle, at)) != std::string_view::npos) {
        const std::size_t line_begin = tex.rfind('\n', at) == std::string_view::npos ? 0 : tex.rfind('\n', at) + 1;
        if (tex.substr(line_begin, at - line_begin).find("\\rSec") != std::string_view::npos)
            return true;
        at += needle.size();
    }
    return false;
}

std::optional<std::string> read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

TreeResult import_from_directory(const std::filesystem::path& directory, std::span<const std::string> names) {
    TreeResult                         result;
    std::error_code                    ec;
    std::vector<std::filesystem::path> files;
    // substrate generic algorithm: directory iteration is an input range with an error channel, not a fold.
    for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
        if (it->path().extension() == ".tex")
            files.push_back(it->path());
    if (ec) {
        result.diagnostics.push_back(
            {Severity::Error, {}, std::format("cannot read the draft source directory '{}'", directory.string())});
        return result;
    }
    std::ranges::sort(files);

    std::map<std::string, ir::Node> found;
    // substrate generic algorithm: each file claims the names it declares among those still unclaimed.
    for (const auto& file : files) {
        const std::optional<std::string> tex = read_text(file);
        if (!tex)
            continue;
        const std::vector<std::string> here = names | std::views::filter([&](const std::string& name) {
                                                  return !found.contains(name) && declares_section(*tex, name);
                                              }) |
                                              std::ranges::to<std::vector<std::string>>();
        if (here.empty())
            continue;
        Result imported = import_sections(*tex, {.sections = here});
        result.sources.push_back(file.string());
        result.diagnostics.append_range(std::move(imported.diagnostics));
        // substrate generic algorithm: pairs each name with the node imported for it.
        for (auto&& [name, node] : std::views::zip(here, imported.document.nodes))
            found.emplace(name, std::move(node));
    }
    // substrate generic algorithm: assembles the result in the order the names came.
    for (const std::string& name : names) {
        if (auto it = found.find(name); it != found.end())
            result.document.nodes.push_back(std::move(it->second));
        else
            result.missing.push_back(name);
    }
    return result;
}

} // namespace beman::specgen::draft_import
