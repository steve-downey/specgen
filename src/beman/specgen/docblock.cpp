// src/beman/specgen/docblock.cpp                                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/docblock.hpp>

#include <beman/specgen/foundation/overloaded.hpp>
#include <beman/specgen/foundation/parse/cursor.hpp>
#include <beman/specgen/foundation/parse/parser.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <ranges>
#include <string>
#include <utility>
#include <variant>

namespace beman::specgen::grammar {

// The docblock scanners below (strip_comment_decorations, parse_inlines, the
// tag/marker head of parse_docblock) are built on the combinator library
// (decision parser-combinators). The three-severity diagnostic
// accumulation in ParseResult sits above them: combinators do the scanning,
// and a combinator failure is converted into an accumulated Diagnostic at the
// call site rather than propagated out (decision expected-error-taxonomy
// assigns docblock parsing to the accumulating/Validation row, not
// fail-fast expected).
namespace parse = beman::specgen::foundation::parse;
using beman::specgen::foundation::overloaded;

namespace {

std::string_view trim(std::string_view s) {
    // The whitespace set matches std::isspace under the classic "C" locale
    // (the only locale docblock text is ever scanned in), stated directly
    // rather than through a per-character predicate call.
    constexpr std::string_view whitespace = " \t\n\v\f\r";
    const auto                 first      = s.find_first_not_of(whitespace);
    if (first == std::string_view::npos)
        return {};
    return s.substr(first, s.find_last_not_of(whitespace) - first + 1);
}

bool is_blank(std::string_view s) { return trim(s).empty(); }

std::vector<std::string_view> split_lines(std::string_view text) {
    // views::split on '\n' produces one subrange per line, including a
    // trailing empty one when text ends in '\n' -- exactly the shape the
    // hand-written scan produced before its own trailing-empty drop below.
    // Each subrange aliases text, so it is rebuilt as a string_view (rather
    // than left as a subrange) immediately, before anything else touches it.
    auto lines = text | std::views::split('\n') |
                 std::views::transform([](auto&& chunk) { return std::string_view(chunk.begin(), chunk.end()); }) |
                 std::ranges::to<std::vector<std::string_view>>();
    // Drop a trailing empty line produced by a final '\n'.
    if (!lines.empty() && lines.back().empty())
        lines.pop_back();
    return lines;
}

std::string chars_to_string(std::vector<char> cs) { return std::string(cs.begin(), cs.end()); }

// -------------------------------------------------------------------------
// strip_comment_decorations: `//!`/`///`/`//` and `/*! ... */` scanners.
// -------------------------------------------------------------------------

bool is_line_ws(char c) { return c == ' ' || c == '\t'; }

// Consumes a `/*` opening a block doc-comment, plus an optional `!` or `*`
// decoration character right after it. Only ever run once, on the whole
// text, after `text.starts_with("/*")` has already established that it
// matches.
auto block_open() {
    return parse::sequence_right(parse::keyword("/*"), parse::opt(parse::char_p('!') | parse::char_p('*')));
}

// Consumes the `//`, `///`, or `//!` decoration introducing a line-comment
// docblock line. The two slashes are mandatory; a following `!` or `/` is an
// optional third decoration character (so `//!` and `///` both strip to
// three characters, bare `//` strips to two). Fails without consuming input
// when the line does not start with `//` at all, so `opt()` at the call site
// turns "no decoration here" into a clean pass-through rather than an error.
auto line_comment_decoration() {
    return parse::sequence_right(parse::keyword("//"), parse::opt(parse::char_p('!') | parse::char_p('/')));
}

// Strips one physical line's leading whitespace and, if present, its comment
// decoration (block `*` or line `//`/`///`/`//!`) together with the single
// space conventionally following it. Lines without decoration pass through
// unchanged past the leading-whitespace strip — `opt()` makes the decoration
// itself optional, and once the leading run of spaces/tabs is gone there is
// no leading space left for the trailing `opt(char_p(' '))` to remove unless
// a decoration match just exposed one, so it is safe to run unconditionally
// in both the block and line-comment cases.
std::string_view strip_line_decoration(std::string_view line, bool block) {
    parse::cursor cur{line};
    cur = parse::many(parse::satisfy(is_line_ws, "line whitespace"))(cur)->rest;
    cur = block ? parse::opt(parse::char_p('*'))(cur)->rest : parse::opt(line_comment_decoration())(cur)->rest;
    cur = parse::opt(parse::char_p(' '))(cur)->rest;
    return cur.remaining();
}

// -------------------------------------------------------------------------
// parse_inlines: backtick code-span and prose-reference scanner.
// -------------------------------------------------------------------------

bool not_backtick(char c) { return c != '`'; }

// A run of plain text up to the next backtick or end of input. `some`
// requires at least one character, matching the original scan's refusal to
// ever emit an empty InlineText.
struct RawText {
    std::string text;
};

// A backtick-delimited span. `terminated` is false when input ran out before
// a closing backtick was found; parse_inlines then reports the diagnostic
// and demotes the unterminated span's content back to plain text (matching
// the original hand scanner, which never produced an InlineCode for a span
// it couldn't close).
struct RawCode {
    std::string text;
    bool        terminated;
};

struct RawRef {
    std::string stable_name;
    bool        terminated;
};

using RawInline = std::variant<RawText, RawCode, RawRef>;

auto text_run() {
    return parse::parser{[](parse::cursor cur) -> parse::parse_result<RawInline> {
        const parse::cursor start = cur;
        // substrate generic algorithm: cursor scan to the earliest of two
        // variable-length delimiters; the cursor has no bulk-advance operation.
        while (!cur.empty() && cur.peek() != '`' && !cur.remaining().starts_with("\\iref{"))
            cur = cur.bump();
        if (cur.position().offset == start.position().offset)
            return std::unexpected(parse::parse_error{cur.position(), "text character"});
        const std::size_t count = cur.position().offset - start.position().offset;
        return parse::parse_state<RawInline>{RawText{std::string(start.remaining().substr(0, count))}, cur};
    }};
}

auto code_run() {
    return parse::lift2(
        parse::sequence_right(parse::char_p('`'), parse::many(parse::satisfy(not_backtick, "code content"))),
        parse::opt(parse::char_p('`')),
        [](std::vector<char> body, std::optional<char> closing) -> RawInline {
            return RawCode{chars_to_string(std::move(body)), closing.has_value()};
        });
}

auto ref_run() {
    return parse::parser{[](parse::cursor cur) -> parse::parse_result<RawInline> {
        constexpr std::string_view prefix = "\\iref{";
        if (!cur.remaining().starts_with(prefix))
            return std::unexpected(parse::parse_error{cur.position(), "\\iref reference"});
        // substrate generic algorithm: consume the fixed prefix while
        // preserving the cursor's source-position accounting.
        for ([[maybe_unused]] char c : prefix)
            cur = cur.bump();
        std::string stable_name;
        // substrate generic algorithm: delimiter scan that simultaneously
        // advances the parse cursor and collects the reference payload.
        while (!cur.empty() && cur.peek() != '}') {
            stable_name.push_back(cur.peek());
            cur = cur.bump();
        }
        const bool terminated = !cur.empty();
        if (terminated)
            cur = cur.bump();
        return parse::parse_state<RawInline>{RawRef{std::move(stable_name), terminated}, cur};
    }};
}

// The three branches have disjoint starts: a backtick, the full `\iref{`
// prefix, or text ending immediately before either delimiter. Each fails
// without consuming when its start does not match, so this ordered choice
// never hits the commit-on-consumed-input hazard that rules out alternating
// over shared-prefix spellings (foundation DIVERGENCES
// OBS-4). Once a branch matches its delimiter it always succeeds.
auto inline_segment() { return code_run() | ref_run() | text_run(); }

// Parse backticked code spans in an assembled paragraph string.
ProseParagraph parse_inlines(std::string_view text, int line, std::vector<Diagnostic>& diags) {
    ProseParagraph para;
    // many() never fails, so the scan below always has a value.
    auto segments = parse::many(inline_segment())(parse::cursor{text});
    // substrate generic algorithm: folds into two accumulators at once (para
    // and diags), and which one -- or both, or neither -- a given segment
    // feeds is decided inside the visited alternative, not by a uniform
    // per-element rule. A ranges::fold_left over a {para, diags} pair
    // accumulator would carry the exact same branching one level down,
    // inside the combining function, which is not an improvement.
    for (RawInline& seg : segments->value) {
        std::visit(overloaded{[&](RawText& t) {
                                  if (!t.text.empty())
                                      para.push_back(InlineText{std::move(t.text)});
                              },
                              [&](RawCode& c) {
                                  if (c.terminated) {
                                      para.push_back(InlineCode{std::move(c.text)});
                                  } else {
                                      diags.push_back({Severity::Error, line, "unterminated ` code span"});
                                      if (!c.text.empty())
                                          para.push_back(InlineText{std::move(c.text)});
                                  }
                              },
                              [&](RawRef& ref) {
                                  if (!ref.terminated) {
                                      diags.push_back({Severity::Error, line, "unterminated \\iref reference"});
                                      if (!ref.stable_name.empty())
                                          para.push_back(InlineText{"\\iref{" + std::move(ref.stable_name)});
                                  } else if (ref.stable_name.empty()) {
                                      diags.push_back({Severity::Error, line, "\\iref requires a stable name"});
                                  } else {
                                      para.push_back(InlineRef{std::move(ref.stable_name)});
                                  }
                              }},
                   seg);
    }
    return para;
}

const MarkerInfo* find_marker(std::string_view name) {
    const auto it = std::ranges::find_if(kMarkers, [name](const MarkerInfo& info) { return info.spelling == name; });
    return it != std::ranges::end(kMarkers) ? &*it : nullptr;
}

// -------------------------------------------------------------------------
// parse_docblock: the `\name` tag head and the `(arg)` marker-argument
// scanner.
// -------------------------------------------------------------------------

bool is_tag_char(char c) { return static_cast<bool>(std::isalnum(static_cast<unsigned char>(c))) || c == '-'; }

// Scans a `\name` tag head: the backslash, then a run of letters, digits and
// hyphens. Every element tag and marker spelling shares the leading `\`
// (and several share letters beyond it), so — per the shared-prefix hazard
// noted above — the name is scanned generically with `many`/`satisfy`
// rather than matched by alternating `keyword()` over the individual
// spellings; the scanned name is dispatched through find_marker /
// element_from_name below, unchanged. Fails without consuming when the line
// does not start with `\`, so `opt()` at the call site distinguishes a tag
// line from a prose line.
auto tag_head() {
    return parse::sequence_right(
        parse::char_p('\\'),
        parse::map(parse::many(parse::satisfy(is_tag_char, "tag name character")), chars_to_string));
}

// Scans a parenthesized marker argument: `(`, a run of non-`)` characters,
// `)`. Used for `\expos(name)`; wrapped in `opt()` at the call site so a
// missing `(` (fails without consuming) is "no argument" while a `(` with no
// closing `)` (fails after consuming) is a genuine, reported error — the
// same commit-on-consumed-input signal that drives `operator|`.
auto paren_arg() {
    return parse::sequence_right(
        parse::char_p('('),
        parse::map(parse::sequence_left(parse::many(parse::satisfy([](char c) { return c != ')'; }, "paren content")),
                                        parse::char_p(')')),
                   chars_to_string));
}

struct TableHead {
    std::string stable_name;
    std::string caption;
};

TableHead parse_table_head(std::string_view after, int line, std::vector<Diagnostic>& diags) {
    TableHead        out;
    std::string_view rest = trim(after);
    if (!rest.starts_with('[')) {
        diags.push_back({Severity::Error, line, "\\lib2dtab2 requires [stable.name]{caption}"});
        return out;
    }
    const std::size_t close = rest.find(']');
    if (close == std::string_view::npos) {
        diags.push_back({Severity::Error, line, "unterminated '[' in \\lib2dtab2"});
        return out;
    }
    out.stable_name = std::string(trim(rest.substr(1, close - 1)));
    if (out.stable_name.empty())
        diags.push_back({Severity::Error, line, "\\lib2dtab2 requires a stable name"});

    rest = trim(rest.substr(close + 1));
    if (!rest.starts_with('{')) {
        diags.push_back({Severity::Error, line, "\\lib2dtab2 requires a {caption}"});
        return out;
    }
    const std::size_t caption_end = rest.rfind('}');
    if (caption_end == std::string_view::npos) {
        diags.push_back({Severity::Error, line, "unterminated '{' in \\lib2dtab2"});
        out.caption = std::string(trim(rest.substr(1)));
        return out;
    }
    out.caption = std::string(trim(rest.substr(1, caption_end - 1)));
    if (out.caption.empty())
        diags.push_back({Severity::Error, line, "\\lib2dtab2 requires a caption"});
    if (!trim(rest.substr(caption_end + 1)).empty())
        diags.push_back({Severity::Warning, line, "trailing text after \\lib2dtab2 ignored"});
    return out;
}

} // namespace

namespace detail {

bool set_expos_name(Markers& markers, std::string arg) {
    const bool had     = markers.expos_name.has_value();
    markers.expos_name = std::move(arg);
    return had;
}

bool set_at_anchor(Markers& markers, std::string arg) {
    const bool had    = markers.at_anchor.has_value();
    markers.at_anchor = std::move(arg);
    return had;
}

bool set_seebelow_target(Markers& markers, std::string arg) {
    const bool had          = markers.seebelow_target.has_value();
    markers.seebelow_target = std::move(arg);
    return had;
}

bool set_group_id(Markers& markers, std::string arg) {
    const bool had   = markers.group_id.has_value();
    markers.group_id = std::move(arg);
    return had;
}

bool set_also_target(Markers& markers, std::string arg) {
    const bool had      = markers.also_target.has_value();
    markers.also_target = std::move(arg);
    return had;
}

} // namespace detail

std::string strip_comment_decorations(std::string_view raw) {
    std::string_view text  = trim(raw);
    bool             block = false;
    if (text.starts_with("/*")) {
        block = true;
        // block_open() is guaranteed to succeed: the starts_with check above
        // already established the "/*" it requires.
        text = block_open()(parse::cursor{text})->rest.remaining();
        if (text.ends_with("*/"))
            text.remove_suffix(2);
    }
    // Separation is a property of the join, not loop-carried state -- the
    // same `bool first` flag latex.cpp's prolog argues against.
    return split_lines(text) |
           std::views::transform([block](std::string_view line) { return strip_line_decoration(line, block); }) |
           std::views::join_with('\n') | std::ranges::to<std::string>();
}

ParseResult parse_docblock(std::string_view raw) {
    ParseResult       result;
    const std::string text  = strip_comment_decorations(raw);
    auto              lines = split_lines(text);

    Docblock& block = result.block;
    auto&     diags = result.diags;

    std::vector<std::string> para_lines; // pending paragraph, joined on flush
    std::vector<std::string> item_lines; // pending authored item, joined on flush
    int                      para_line  = 0;
    int                      item_line  = 0;
    bool                     in_itemize = false;
    bool                     item_open  = false;
    enum class TablePart { None, Caption, Column1, Column2, RowHeader, Cell1, Cell2 };
    bool                     in_table       = false;
    bool                     table_terminal = false;
    TablePart                table_part     = TablePart::None;
    std::vector<std::string> table_lines;
    int                      table_part_line = 0;
    std::size_t              table_columns   = 0;
    std::size_t              table_rows      = 0;
    std::size_t              table_cells     = 0;
    int                      last_rank       = -1;
    // The element that set `last_rank`, carried alongside it so the
    // ordering note can name what the offending tag came after: "\effects
    // appears after \remarks" says which of the two lines to move, where "out
    // of canonical order" left the reader to work it out. Only meaningful
    // once last_rank > -1, which is the same condition the note fires under.
    ir::ElementKind last_kind                 = ir::ElementKind::Effects;
    bool            prose_before_tag_reported = false;

    auto current_element = [&]() -> Element* { return block.elements.empty() ? nullptr : &block.elements.back(); };

    auto flush_paragraph = [&] {
        if (para_lines.empty())
            return;
        std::string joined = para_lines | std::views::join_with(' ') | std::ranges::to<std::string>();
        para_lines.clear();
        std::string_view content = trim(joined);
        if (content.empty())
            return;
        if (Element* e = current_element()) {
            e->paragraphs.push_back(parse_inlines(content, para_line, diags));
        } else if (!std::exchange(prose_before_tag_reported, true)) {
            diags.push_back({Severity::Error, para_line, "prose before first element tag"});
        }
    };

    auto flush_item = [&] {
        if (!item_open)
            return;
        item_open          = false;
        std::string joined = item_lines | std::views::join_with(' ') | std::ranges::to<std::string>();
        item_lines.clear();
        const std::string_view content = trim(joined);
        if (content.empty()) {
            diags.push_back({Severity::Error, item_line, "\\item requires content"});
            return;
        }
        current_element()->items.push_back(parse_inlines(content, item_line, diags));
    };

    auto flush_table_part = [&] {
        if (table_part == TablePart::None)
            return;
        const std::string joined = table_lines | std::views::join_with(' ') | std::ranges::to<std::string>();
        table_lines.clear();
        const std::string_view content = trim(joined);
        if (content.empty()) {
            diags.push_back(
                {Severity::Error, table_part_line, "table caption, header, and cell text cannot be empty"});
            table_part = TablePart::None;
            return;
        }
        if (Element* element = current_element(); element != nullptr && element->table) {
            ProseParagraph paragraph = parse_inlines(content, table_part_line, diags);
            switch (table_part) {
            case TablePart::Caption:
                element->table->caption = std::move(paragraph);
                break;
            case TablePart::Column1:
                element->table->column1 = std::move(paragraph);
                break;
            case TablePart::Column2:
                element->table->column2 = std::move(paragraph);
                break;
            case TablePart::RowHeader:
                element->table->rows.back().header = std::move(paragraph);
                break;
            case TablePart::Cell1:
                element->table->rows.back().cell1 = std::move(paragraph);
                break;
            case TablePart::Cell2:
                element->table->rows.back().cell2 = std::move(paragraph);
                break;
            case TablePart::None:
                std::unreachable();
            }
        }
        table_part = TablePart::None;
    };

    auto finish_table = [&](int lineno, bool missing_end) {
        flush_table_part();
        if (missing_end)
            diags.push_back({Severity::Error, lineno, "missing \\endlib2dtab2"});
        if (table_columns != 2)
            diags.push_back(
                {Severity::Error,
                 lineno,
                 std::format("\\lib2dtab2 requires exactly two \\column entries; found {}", table_columns)});
        if (table_rows == 0)
            diags.push_back({Severity::Error, lineno, "\\lib2dtab2 requires at least one \\row"});
        else if (table_cells != 2)
            diags.push_back(
                {Severity::Error,
                 lineno,
                 std::format("the final \\row requires exactly two \\cell entries; found {}", table_cells)});
        in_table       = false;
        table_terminal = true;
        table_part     = TablePart::None;
    };

    auto handle_marker = [&](const MarkerInfo& info, std::string_view after, int lineno) {
        const std::string name{info.spelling};
        bool              arg_replaced_value = false;

        switch (info.arity) {
        case MarkerArity::Flag:
            break;
        case MarkerArity::ParenOptional: { // \expos(name)
            auto arg = parse::opt(paren_arg())(parse::cursor{after});
            if (!arg.has_value()) {
                diags.push_back({Severity::Error, lineno, "unterminated '(' in \\" + name});
                return;
            }
            if (arg->value) {
                std::string parsed{trim(*arg->value)};
                after              = trim(arg->rest.remaining());
                arg_replaced_value = info.set_arg(block.markers, std::move(parsed));
            }
            break;
        }
        case MarkerArity::RestRequired: { // \at anchor, \group id
            std::string_view arg = trim(after);
            if (arg.empty()) {
                diags.push_back({Severity::Error, lineno, std::string(info.missing_arg_error)});
                return;
            }
            arg_replaced_value = info.set_arg(block.markers, std::string(arg));
            after              = {};
            break;
        }
        case MarkerArity::RestOptional: { // \seebelow [target], \also [id]
            std::string_view arg = trim(after);
            if (!arg.empty()) {
                info.set_arg(block.markers, std::string(arg));
                if (name == "seebelow" && arg != "noexcept" && arg != "explicit")
                    diags.push_back(
                        {Severity::Error,
                         lineno,
                         std::format("unknown \\seebelow target '{}'; expected 'noexcept' or 'explicit'", arg)});
            }
            after = {};
            break;
        }
        }

        // Markers with no dedicated flag (arity RestRequired, e.g. \at and
        // \group) have nothing else to signal a repeat occurrence, so the
        // setter's report of an already-present value stands in for it.
        // Flagged markers get the same diagnostic below, keyed off the flag.
        if (info.flag == nullptr && arg_replaced_value)
            diags.push_back({Severity::Warning, lineno, "duplicate \\" + name + " marker"});

        if (!trim(after).empty())
            diags.push_back({Severity::Warning, lineno, "trailing text after \\" + name + " ignored"});
        if (info.flag != nullptr) {
            if (block.markers.*(info.flag))
                diags.push_back({Severity::Warning, lineno, "duplicate \\" + name + " marker"});
            block.markers.*(info.flag) = true;
        }
    };

    // substrate generic algorithm: multi-way stateful sequencing across
    // lines, not a fold. It threads five pieces of state (para_lines,
    // para_line, last_rank, prose_before_tag_reported, block.elements) and
    // calls flush_paragraph() from several different branches depending on
    // what kind of line was just read; a fold whose accumulator bundles all
    // five and whose combining function contains the same branching is the
    // same state machine wearing a different hat, not a simplification.
    // `i` itself is read-only here -- used only to derive lineno and to read
    // lines[i] at the current position, never to write elsewhere -- so per
    // CODING_RULES's index tell it still drops in favor of enumerate.
    for (auto [i, line] : lines | std::views::enumerate) {
        const int lineno = static_cast<int>(i) + 1;

        if (is_blank(line)) {
            if (in_table)
                continue;
            if (in_itemize)
                flush_item();
            else
                flush_paragraph();
            continue;
        }

        std::string_view stripped = trim(line);
        auto             tag      = parse::opt(tag_head())(parse::cursor{stripped});

        if (tag->value && in_table && *tag->value != "iref") {
            const std::string_view name  = *tag->value;
            const std::string_view after = trim(tag->rest.remaining());
            if (name == "lib2dtab2") {
                diags.push_back({Severity::Error, lineno, "nested \\lib2dtab2 is not allowed"});
                continue;
            }
            if (name == "column") {
                flush_table_part();
                if (table_rows != 0)
                    diags.push_back({Severity::Error, lineno, "\\column must precede every \\row"});
                ++table_columns;
                if (table_columns > 2) {
                    diags.push_back({Severity::Error, lineno, "\\lib2dtab2 accepts exactly two \\column entries"});
                    table_part = TablePart::None;
                } else {
                    table_part = table_columns == 1 ? TablePart::Column1 : TablePart::Column2;
                }
                table_part_line = lineno;
                if (table_part != TablePart::None)
                    table_lines.emplace_back(after);
                continue;
            }
            if (name == "row") {
                flush_table_part();
                if (table_columns != 2)
                    diags.push_back({Severity::Error, lineno, "\\row requires two preceding \\column entries"});
                if (table_rows != 0 && table_cells != 2)
                    diags.push_back({Severity::Error,
                                     lineno,
                                     std::format("the preceding \\row requires exactly two \\cell entries; found {}",
                                                 table_cells)});
                ++table_rows;
                table_cells = 0;
                if (Element* element = current_element(); element != nullptr && element->table)
                    element->table->rows.emplace_back();
                table_part      = TablePart::RowHeader;
                table_part_line = lineno;
                table_lines.emplace_back(after);
                continue;
            }
            if (name == "cell") {
                flush_table_part();
                if (table_rows == 0) {
                    diags.push_back({Severity::Error, lineno, "\\cell requires a preceding \\row"});
                    continue;
                }
                ++table_cells;
                if (table_cells > 2) {
                    diags.push_back({Severity::Error, lineno, "each \\row accepts exactly two \\cell entries"});
                    table_part = TablePart::None;
                } else {
                    table_part = table_cells == 1 ? TablePart::Cell1 : TablePart::Cell2;
                }
                table_part_line = lineno;
                if (table_part != TablePart::None)
                    table_lines.emplace_back(after);
                continue;
            }
            if (name == "endlib2dtab2") {
                if (!after.empty())
                    diags.push_back({Severity::Warning, lineno, "trailing text after \\endlib2dtab2 ignored"});
                finish_table(lineno, false);
                continue;
            }
            if (ir::element_from_name(name)) {
                finish_table(lineno, true);
                // Process the new element below after closing the malformed table.
            } else {
                diags.push_back({Severity::Error, lineno, std::format("unexpected \\{} inside \\lib2dtab2", name)});
                continue;
            }
        }

        if (tag->value && *tag->value != "iref") {
            std::string_view name  = *tag->value;
            std::string_view after = tag->rest.remaining();

            if (auto kind = ir::element_from_name(name)) {
                flush_item();
                flush_paragraph();
                if (std::ranges::any_of(block.elements, [kind](const Element& e) { return e.kind == *kind; }))
                    diags.push_back(
                        {Severity::Warning, lineno, "duplicate \\" + std::string(name) + " element; both kept"});
                if (ir::canonical_rank(*kind) < last_rank)
                    diags.push_back({Severity::Note,
                                     lineno,
                                     "\\" + std::string(name) + " appears after \\" +
                                         std::string(ir::element_name(last_kind)) + "; output is canonicalized"});
                if (ir::canonical_rank(*kind) > last_rank) {
                    last_rank = ir::canonical_rank(*kind);
                    last_kind = *kind;
                }
                block.elements.push_back(Element{.kind = *kind, .line = lineno});
                in_itemize            = false;
                table_terminal        = false;
                std::string_view lead = trim(after);
                if (!lead.empty()) {
                    para_lines.emplace_back(lead);
                    para_line = lineno;
                }
            } else if (name == "lib2dtab2") {
                flush_item();
                flush_paragraph();
                if (table_terminal) {
                    diags.push_back({Severity::Error, lineno, "an element can carry only one terminal \\lib2dtab2"});
                    continue;
                }
                const TableHead head = parse_table_head(after, lineno, diags);
                if (current_element() == nullptr)
                    diags.push_back({Severity::Error, lineno, "\\lib2dtab2 requires a preceding element tag"});
                else {
                    const ir::ElementKind kind = current_element()->kind;
                    if (std::ranges::any_of(block.elements | std::views::take(block.elements.size() - 1),
                                            [kind](const Element& element) {
                                                return element.kind == kind && element.table.has_value();
                                            }))
                        diags.push_back({Severity::Error,
                                         lineno,
                                         "an element kind can carry only one \\lib2dtab2; combine the rows"});
                    current_element()->table = Table2D{.stable_name = head.stable_name};
                }
                in_table        = true;
                in_itemize      = false;
                table_columns   = 0;
                table_rows      = 0;
                table_cells     = 0;
                table_part      = TablePart::Caption;
                table_part_line = lineno;
                table_lines.emplace_back(head.caption);
            } else if (name == "endlib2dtab2" || name == "column" || name == "row" || name == "cell") {
                diags.push_back({Severity::Error, lineno, std::format("\\{} appears outside \\lib2dtab2", name)});
            } else if (table_terminal) {
                diags.push_back({Severity::Error,
                                 lineno,
                                 "a \\lib2dtab2 is terminal within its element; start a new element tag"});
            } else if (name == "item") {
                flush_paragraph();
                if (current_element() == nullptr) {
                    diags.push_back({Severity::Error, lineno, "\\item requires a preceding element tag"});
                    continue;
                }
                flush_item();
                in_itemize                  = true;
                item_open                   = true;
                item_line                   = lineno;
                const std::string_view lead = trim(after);
                if (!lead.empty())
                    item_lines.emplace_back(lead);
            } else if (name == "verbatim-synopsis") {
                flush_item();
                flush_paragraph();
                handle_marker(*find_marker(name), after, lineno);
                block.verbatim_synopsis =
                    lines | std::views::drop(i + 1) | std::views::join_with('\n') | std::ranges::to<std::string>();
                break;
            } else if (name == "verbatim-itemdecl") {
                flush_item();
                flush_paragraph();
                handle_marker(*find_marker(name), after, lineno);
                block.verbatim_itemdecl =
                    lines | std::views::drop(i + 1) | std::views::join_with('\n') | std::ranges::to<std::string>();
                break;
            } else if (const MarkerInfo* info = find_marker(name)) {
                flush_item();
                flush_paragraph();
                // A marker carries no description content, so it does not
                // reopen paragraph mode after a list. Only a new element can
                // do that without moving prose ahead of the itemize in IR.
                handle_marker(*info, after, lineno);
            } else {
                diags.push_back({Severity::Error, lineno, "unknown tag \\" + std::string(name)});
            }
            continue;
        }

        if (in_table) {
            if (table_part == TablePart::None)
                diags.push_back(
                    {Severity::Error, lineno, "table continuation has no active caption, header, or cell"});
            else
                table_lines.emplace_back(stripped);
            continue;
        }

        if (table_terminal) {
            diags.push_back(
                {Severity::Error, lineno, "prose after \\lib2dtab2 cannot be represented; start a new element tag"});
            continue;
        }

        if (in_itemize) {
            if (!item_open) {
                diags.push_back(
                    {Severity::Error, lineno, "prose after \\item cannot be represented; start another \\item"});
                continue;
            }
            item_lines.emplace_back(stripped);
            continue;
        }

        // Prose line: accumulate into the pending paragraph.
        if (para_lines.empty())
            para_line = lineno;
        para_lines.emplace_back(stripped);
    }
    if (in_table)
        finish_table(lines.empty() ? 0 : static_cast<int>(lines.size()), true);
    flush_item();
    flush_paragraph();

    // Cross checks.
    auto has_element = [&](ir::ElementKind k) {
        return std::any_of(
            block.elements.begin(), block.elements.end(), [k](const Element& e) { return e.kind == k; });
    };
    if (block.markers.effects_equiv && has_element(ir::ElementKind::Effects))
        diags.push_back({Severity::Error, 0, "\\effects and \\effects-equiv are mutually exclusive"});
    if (block.markers.returns_equiv && has_element(ir::ElementKind::Returns))
        diags.push_back({Severity::Error, 0, "\\returns and \\returns-equiv are mutually exclusive"});
    if (block.markers.also && !block.elements.empty())
        diags.push_back({Severity::Warning, 0, "\\also block should carry no elements; they are ignored by grouping"});
    if (block.markers.group_id && block.markers.also)
        diags.push_back({Severity::Error, 0, "\\group and \\also are mutually exclusive"});
    if (block.markers.impdef && block.markers.seebelow)
        diags.push_back({Severity::Error, 0, "\\impdef and \\seebelow are mutually exclusive"});
    if (block.markers.expos_name)
        block.markers.expos = true;

    return result;
}

} // namespace beman::specgen::grammar
