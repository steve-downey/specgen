// src/beman/specgen/diff.cpp                                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/diff.hpp>

#include <beman/specgen/diff/sequence.hpp>
#include <beman/specgen/foundation/enumeration.hpp>
#include <beman/specgen/foundation/overloaded.hpp>
#include <beman/specgen/ir.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace beman::specgen::diff {

namespace {

using beman::specgen::foundation::overloaded;
using namespace std::string_view_literals;

inline constexpr auto kContextNames = foundation::table<Context>(foundation::is<Context::Changed>("changed"sv),
                                                                 foundation::is<Context::All>("all"sv),
                                                                 foundation::is<Context::Items>("items"sv));

// --- similarity -------------------------------------------------------------

// Jaccard over two token-key sets: the share of the words (or code tokens)
// two pieces of text have in common, out of all the words either has. Two
// empty pieces are the same piece.
double similarity(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    if (a.empty() && b.empty())
        return 1.0;
    const std::set<std::string> sa(a.begin(), a.end());
    const std::set<std::string> sb(b.begin(), b.end());
    const auto                  common =
        static_cast<double>(std::ranges::count_if(sa, [&](const std::string& k) { return sb.contains(k); }));
    return common / (static_cast<double>(sa.size() + sb.size()) - common);
}

// --- prose tokens -------------------------------------------------------------

bool is_space(char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'; }

// The words of a text, each carrying the whitespace after it, the way `wdiff`
// and `git diff --word-diff` cut: a leading run of whitespace is a word of
// its own, so that no byte of the text is lost to the cut.
std::vector<std::string> words(std::string_view text) {
    std::vector<std::string> out;
    std::size_t              pos = 0;
    // substrate generic algorithm: a position-tracking scan cutting the text
    // at word boundaries is the tokenizer itself, with nothing smaller to
    // fold over.
    while (pos < text.size()) {
        std::size_t end = pos;
        if (!is_space(text[pos]))
            // substrate generic algorithm: the scan's inner step, to the end
            // of the word.
            while (end < text.size() && !is_space(text[end]))
                ++end;
        // substrate generic algorithm: the scan's inner step, over the
        // whitespace the word carries.
        while (end < text.size() && is_space(text[end]))
            ++end;
        out.emplace_back(text.substr(pos, end - pos));
        pos = end;
    }
    return out;
}

std::string_view rstripped(std::string_view text) {
    // substrate generic algorithm: a scan back over trailing whitespace is
    // the trim itself.
    while (!text.empty() && is_space(text.back()))
        text.remove_suffix(1);
    return text;
}

// A prose token is one plain inline: a single word of a TextInline, or an
// inline the diff never splits (a code inline, a cross-reference, a concept
// name). Two words are the same word whatever whitespace follows them, so a
// change of spacing alone is no change; a kept token takes the current
// revision's spacing.
using ProseToken = ir::PlainInline;

bool same_prose(const ProseToken& a, const ProseToken& b) {
    const auto* ta = std::get_if<ir::TextInline>(&a);
    const auto* tb = std::get_if<ir::TextInline>(&b);
    if (ta != nullptr && tb != nullptr)
        return rstripped(ta->text) == rstripped(tb->text);
    return a == b;
}

// `prose_tokens`' dispatch over the four plain inline kinds; the fifth, a
// changed run in an input that should carry none, flattens to its contents.
struct PlainTokenizer {
    std::vector<ProseToken> operator()(const ir::TextInline& v) const {
        return words(v.text) |
               std::views::transform([](std::string word) -> ProseToken { return ir::TextInline{std::move(word)}; }) |
               std::ranges::to<std::vector>();
    }
    std::vector<ProseToken> operator()(const ir::CodeInline& v) const { return {v}; }
    std::vector<ProseToken> operator()(const ir::RefInline& v) const { return {v}; }
    std::vector<ProseToken> operator()(const ir::ConceptRef& v) const { return {v}; }
};

struct Tokenizer : PlainTokenizer {
    using PlainTokenizer::operator();
    std::vector<ProseToken> operator()(const ir::ChangedInline& run) const {
        return run.inlines | std::views::transform([](const ir::PlainInline& piece) {
                   return std::visit(overloaded{PlainTokenizer{}}, piece);
               }) |
               std::views::join | std::ranges::to<std::vector>();
    }
};

std::vector<ProseToken> prose_tokens(const ir::Paragraph& paragraph) {
    return paragraph |
           std::views::transform([](const ir::Inline& piece) { return std::visit(overloaded{Tokenizer{}}, piece); }) |
           std::views::join | std::ranges::to<std::vector>();
}

// A token's key for the similarity measure: what it says, with its kind in
// front so a word and a code inline of the same spelling stay distinct.
struct TokenKey {
    std::string operator()(const ir::TextInline& v) const { return "t:" + std::string(rstripped(v.text)); }
    std::string operator()(const ir::CodeInline& v) const { return "c:" + v.code.text; }
    std::string operator()(const ir::RefInline& v) const { return "r:" + v.stable_name; }
    std::string operator()(const ir::ConceptRef& v) const { return "k:" + v.name; }
};

std::vector<std::string> token_keys(const std::vector<ProseToken>& tokens) {
    return tokens | std::views::transform([](const ProseToken& t) { return std::visit(overloaded{TokenKey{}}, t); }) |
           std::ranges::to<std::vector>();
}

bool similar_paragraphs(const ir::Paragraph& a, const ir::Paragraph& b, const Options& options) {
    return similarity(token_keys(prose_tokens(a)), token_keys(prose_tokens(b))) >= options.pairing_threshold;
}

// Appends one plain inline to a run, joining two adjacent text inlines back
// into one: the words were cut apart only for the diff.
void append_plain(std::vector<ir::PlainInline>& run, ir::PlainInline piece) {
    auto* text = std::get_if<ir::TextInline>(&piece);
    auto* last = run.empty() ? nullptr : std::get_if<ir::TextInline>(&run.back());
    if (text != nullptr && last != nullptr)
        last->text += text->text;
    else
        run.push_back(std::move(piece));
}

// A plain inline as an inline of a paragraph.
ir::Inline widen(ir::PlainInline piece) {
    return std::visit([](auto&& v) -> ir::Inline { return std::forward<decltype(v)>(v); }, std::move(piece));
}

void append_inline(ir::Paragraph& out, ir::PlainInline piece) {
    auto* text = std::get_if<ir::TextInline>(&piece);
    auto* last = out.empty() ? nullptr : std::get_if<ir::TextInline>(&out.back());
    if (text != nullptr && last != nullptr)
        last->text += text->text;
    else
        out.push_back(widen(std::move(piece)));
}

// The paragraph a token script spells: kept tokens as plain inlines, each
// change run as one marked run -- deletions first, then insertions, which is
// the order `join_islands` leaves a change run in.
ir::Paragraph assemble_paragraph(const std::vector<ProseToken>& a,
                                 const std::vector<ProseToken>& b,
                                 const std::vector<Edit>&       script) {
    struct State {
        ir::Paragraph                out;
        std::optional<ir::Change>    open;
        std::vector<ir::PlainInline> run;
    };
    const auto flush = [](State& s) {
        if (s.open && !s.run.empty())
            s.out.push_back(ir::ChangedInline{*s.open, std::move(s.run)});
        s.run.clear();
        s.open.reset();
    };
    State state = std::ranges::fold_left(script, State{}, [&](State s, const Edit& e) {
        const ir::Change change = e.op == Op::Keep     ? ir::Change::Same
                                  : e.op == Op::Delete ? ir::Change::Removed
                                                       : ir::Change::Added;
        if (change == ir::Change::Same) {
            flush(s);
            append_inline(s.out, b[e.b]);
            return s;
        }
        if (s.open != change)
            flush(s);
        s.open = change;
        append_plain(s.run, e.op == Op::Delete ? a[e.a] : b[e.b]);
        return s;
    });
    flush(state);
    return std::move(state.out);
}

ir::Paragraph diff_paragraph(const ir::Paragraph& a, const ir::Paragraph& b, const Options& options) {
    if (a == b)
        return b;
    const std::vector<ProseToken> ta = prose_tokens(a);
    const std::vector<ProseToken> tb = prose_tokens(b);
    return assemble_paragraph(ta, tb, join_islands(edit_script(ta, tb, same_prose), options.island));
}

// A whole paragraph marked, as one run holding everything it says.
ir::Paragraph marked_paragraph(const ir::Paragraph& paragraph, ir::Change change) {
    std::vector<ir::PlainInline> run;
    std::ranges::for_each(prose_tokens(paragraph), [&](const ProseToken& t) { append_plain(run, t); });
    if (run.empty())
        return {};
    return {ir::ChangedInline{change, std::move(run)}};
}

// Paragraph lists (list items, table rows' cells by row): paired by
// similarity through the sequence diff, pairs diffed word by word, the rest
// marked whole.
std::vector<ir::Paragraph>
diff_paragraph_list(const std::vector<ir::Paragraph>& a, const std::vector<ir::Paragraph>& b, const Options& options) {
    if (a == b)
        return b;
    const auto script = edit_script(
        a, b, [&](const ir::Paragraph& x, const ir::Paragraph& y) { return similar_paragraphs(x, y, options); });
    return script | std::views::transform([&](const Edit& e) {
               if (e.op == Op::Keep)
                   return diff_paragraph(a[e.a], b[e.b], options);
               if (e.op == Op::Delete)
                   return marked_paragraph(a[e.a], ir::Change::Removed);
               return marked_paragraph(b[e.b], ir::Change::Added);
           }) |
           std::ranges::to<std::vector>();
}

// --- code tokens --------------------------------------------------------------

// One line of a CodeText, its spans' offsets relative to the line.
struct Line {
    std::string           text;
    std::vector<ir::Span> spans;

    friend bool operator==(const Line&, const Line&) = default;
};

std::vector<Line> lines_of(const ir::CodeText& code) {
    std::vector<Line> out;
    std::size_t       start = 0;
    // substrate generic algorithm: a position-tracking scan cutting the text
    // at line ends and assigning each span to the line holding it.
    while (start <= code.text.size()) {
        const std::size_t end  = code.text.find('\n', start);
        const std::size_t stop = end == std::string::npos ? code.text.size() : end;
        Line              line{code.text.substr(start, stop - start), {}};
        line.spans = code.spans |
                     std::views::filter([&](const ir::Span& s) { return s.begin >= start && s.end <= stop; }) |
                     std::views::transform([&](ir::Span s) {
                         s.begin -= start;
                         s.end -= start;
                         return s;
                     }) |
                     std::ranges::to<std::vector>();
        out.push_back(std::move(line));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return out;
}

bool is_word_char(char ch) { return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_'; }

// One token of a code line: a span, whole, or a lexical token -- an
// identifier or number, or one punctuation character -- with the whitespace
// after it. A span inside a token's whitespace never happens: spans cover
// identifiers.
struct CodeToken {
    std::string             text;
    std::optional<ir::Span> span; // relative to the token, covering [0, text.size()) less trailing space
};

// Two tokens are the same token whatever whitespace follows them, as two
// words are; the kept one takes the current revision's spacing.
bool same_code_token(const CodeToken& a, const CodeToken& b) {
    return rstripped(a.text) == rstripped(b.text) && a.span == b.span;
}

std::vector<CodeToken> code_tokens(const Line& line) {
    std::vector<CodeToken> out;
    std::size_t            pos        = 0;
    auto                   span       = line.spans.begin();
    const auto             take_space = [&](std::size_t end) {
        // substrate generic algorithm: the lexer's inner step, over the
        // whitespace a token carries.
        while (end < line.text.size() && is_space(line.text[end]))
            ++end;
        return end;
    };
    // substrate generic algorithm: a position-tracking scan over the line,
    // the lexer itself.
    while (pos < line.text.size()) {
        if (span != line.spans.end() && span->begin == pos) {
            const std::size_t end = take_space(span->end);
            ir::Span          relative{0, span->end - pos, span->kind, span->payload};
            out.push_back(CodeToken{line.text.substr(pos, end - pos), relative});
            ++span;
            pos = end;
            continue;
        }
        std::size_t end = pos;
        if (is_space(line.text[pos])) {
            end = take_space(pos);
        } else if (is_word_char(line.text[pos])) {
            // substrate generic algorithm: the lexer's inner step, to the end
            // of an identifier or number, stopping short of a span.
            while (end < line.text.size() && is_word_char(line.text[end]) &&
                   (span == line.spans.end() || end < span->begin))
                ++end;
            end = take_space(end);
        } else {
            end = take_space(pos + 1);
        }
        out.push_back(CodeToken{line.text.substr(pos, end - pos), std::nullopt});
        pos = end;
    }
    return out;
}

std::vector<std::string> code_keys(const std::vector<CodeToken>& tokens) {
    return tokens | std::views::transform([](const CodeToken& t) { return std::string(rstripped(t.text)); }) |
           std::ranges::to<std::vector>();
}

// A CodeText under construction: text, spans and change runs with absolute
// offsets, lines joined by '\n' as they come.
struct BuiltCode {
    ir::CodeText code;

    void append(std::string_view text, std::optional<ir::Span> span, ir::Change change) {
        const std::size_t at = code.text.size();
        code.text += text;
        if (span) {
            span->begin += at;
            span->end += at;
            code.spans.push_back(*span);
        }
        if (change == ir::Change::Same || text.empty())
            return;
        if (!code.changes.empty() && code.changes.back().end == at && code.changes.back().change == change)
            code.changes.back().end = at + text.size();
        else
            code.changes.push_back(ir::ChangeRun{at, at + text.size(), change});
    }
    // A whole line marked: the mark starts after the indentation, so a
    // backend's escape wraps the line's text and not the spaces before it.
    void append_line(const Line& line, ir::Change change) {
        const std::size_t at     = code.text.size();
        const std::size_t indent = std::min(line.text.size(), line.text.find_first_not_of(" \t"));
        code.text += line.text;
        code.spans.append_range(line.spans | std::views::transform([at](ir::Span s) {
                                    s.begin += at;
                                    s.end += at;
                                    return s;
                                }));
        if (change != ir::Change::Same && indent < line.text.size())
            code.changes.push_back(ir::ChangeRun{at + indent, at + line.text.size(), change});
    }
    void newline() { code.text += '\n'; }
};

// Two similar lines, token by token.
void append_merged_line(BuiltCode& out, const Line& a, const Line& b, const Options& options) {
    const std::vector<CodeToken> ta     = code_tokens(a);
    const std::vector<CodeToken> tb     = code_tokens(b);
    const std::vector<Edit>      script = join_islands(edit_script(ta, tb, same_code_token), options.island);
    std::ranges::for_each(script, [&](const Edit& e) {
        if (e.op == Op::Keep)
            out.append(tb[e.b].text, tb[e.b].span, ir::Change::Same);
        else if (e.op == Op::Delete)
            out.append(ta[e.a].text, ta[e.a].span, ir::Change::Removed);
        else
            out.append(tb[e.b].text, tb[e.b].span, ir::Change::Added);
    });
}

ir::CodeText diff_code(const ir::CodeText& a, const ir::CodeText& b, const Options& options) {
    if (a == b)
        return b;
    const std::vector<Line> la     = lines_of(a);
    const std::vector<Line> lb     = lines_of(b);
    const std::vector<Edit> script = join_islands(edit_script(la, lb), 0);

    BuiltCode  out;
    bool       first      = true;
    const auto line_break = [&] {
        if (!first)
            out.newline();
        first = false;
    };
    // Each change run of the script: its deleted lines paired in order with
    // its inserted ones while they are similar enough to diff token by token;
    // the rest whole, removed lines first.
    const auto emit_change_run = [&](std::span<const Edit> run) {
        const std::vector<std::size_t> deleted = run |
                                                 std::views::filter([](const Edit& e) { return e.op == Op::Delete; }) |
                                                 std::views::transform(&Edit::a) | std::ranges::to<std::vector>();
        const std::vector<std::size_t> inserted =
            run | std::views::filter([](const Edit& e) { return e.op == Op::Insert; }) |
            std::views::transform(&Edit::b) | std::ranges::to<std::vector>();
        std::vector<std::size_t> added_tail;
        // substrate generic algorithm: a walk over two index lists in step,
        // whose outcome for a pair decides where the second index goes.
        for (std::size_t k = 0; k < std::max(deleted.size(), inserted.size()); ++k) {
            const bool has_d = k < deleted.size();
            const bool has_i = k < inserted.size();
            if (has_d && has_i &&
                similarity(code_keys(code_tokens(la[deleted[k]])), code_keys(code_tokens(lb[inserted[k]]))) >=
                    options.pairing_threshold) {
                line_break();
                append_merged_line(out, la[deleted[k]], lb[inserted[k]], options);
                continue;
            }
            if (has_d) {
                line_break();
                out.append_line(la[deleted[k]], ir::Change::Removed);
            }
            if (has_i)
                added_tail.push_back(inserted[k]);
        }
        std::ranges::for_each(added_tail, [&](std::size_t j) {
            line_break();
            out.append_line(lb[j], ir::Change::Added);
        });
    };
    std::ranges::for_each(script | std::views::chunk_by([](const Edit& x, const Edit& y) {
                              return (x.op == Op::Keep) == (y.op == Op::Keep);
                          }),
                          [&](auto&& chunk) {
                              const std::span<const Edit> run(chunk.begin(), chunk.end());
                              if (run.front().op == Op::Keep) {
                                  std::ranges::for_each(run, [&](const Edit& e) {
                                      line_break();
                                      out.append_line(lb[e.b], ir::Change::Same);
                                  });
                              } else {
                                  emit_change_run(run);
                              }
                          });
    std::ranges::sort(out.code.spans, {}, &ir::Span::begin);
    return std::move(out.code);
}

// A whole CodeText marked, one run per line so no run crosses a line end.
ir::CodeText marked_code(const ir::CodeText& code, ir::Change change) {
    BuiltCode out;
    bool      first = true;
    std::ranges::for_each(lines_of(code), [&](const Line& line) {
        if (!first)
            out.newline();
        first = false;
        out.append_line(line, change);
    });
    return std::move(out.code);
}

// Signatures: an overload set's blocks, paired exactly first and then by
// similarity, the rest whole.
std::vector<ir::CodeText>
diff_code_list(const std::vector<ir::CodeText>& a, const std::vector<ir::CodeText>& b, const Options& options) {
    if (a == b)
        return b;
    const auto similar = [&](const ir::CodeText& x, const ir::CodeText& y) {
        return x == y || similarity(code_keys(code_tokens(Line{x.text, x.spans})),
                                    code_keys(code_tokens(Line{y.text, y.spans}))) >= options.pairing_threshold;
    };
    const std::vector<Edit> script = edit_script(a, b, similar);
    return script | std::views::transform([&](const Edit& e) {
               if (e.op == Op::Keep)
                   return diff_code(a[e.a], b[e.b], options);
               if (e.op == Op::Delete)
                   return marked_code(a[e.a], ir::Change::Removed);
               return marked_code(b[e.b], ir::Change::Added);
           }) |
           std::ranges::to<std::vector>();
}

// --- bodies ----------------------------------------------------------------------

template <class Block>
std::vector<Block> diff_blocks(const std::vector<Block>& a, const std::vector<Block>& b, const Options& options);

// Whether two blocks of one kind are the same block edited. A visitor over
// the first with the second in hand; a class template so that the five-kind
// informative variant instantiates only the cases it has.
template <class Block>
struct Pairable {
    const Block&   other;
    const Options& options;

    bool operator()(const ir::BodyParagraph& x) const {
        return similar_paragraphs(x.text, std::get<ir::BodyParagraph>(other).text, options);
    }
    bool operator()(const ir::Itemize&) const { return true; }
    bool operator()(const ir::Table2D& x) const { return x.stable_name == std::get<ir::Table2D>(other).stable_name; }
    bool operator()(const ir::Table1D& x) const { return x.stable_name == std::get<ir::Table1D>(other).stable_name; }
    bool operator()(const ir::EquivalentTo&) const { return true; }
    bool operator()(const ir::AuthoredCode&) const { return true; }
    bool operator()(const ir::Note&) const { return true; }
    bool operator()(const ir::Example&) const { return true; }
};

template <class Block>
bool pairable(const Block& x, const Block& y, const Options& options) {
    return x.index() == y.index() && std::visit(overloaded{Pairable<Block>{y, options}}, x);
}

// Two paired blocks, diffed inside. The result is the current block's shape
// with the baseline's text folded in as marks; its own mark stays `Same`.
template <class Block>
struct BlockDiffer {
    const Block&   other;
    const Options& options;

    Block operator()(const ir::BodyParagraph& x) const {
        const auto& y = std::get<ir::BodyParagraph>(other);
        return ir::BodyParagraph{diff_paragraph(x.text, y.text, options), y.continuation};
    }
    Block operator()(const ir::Itemize& x) const {
        return ir::Itemize{diff_paragraph_list(x.items, std::get<ir::Itemize>(other).items, options)};
    }
    Block operator()(const ir::Table2D& x) const {
        const auto& y     = std::get<ir::Table2D>(other);
        ir::Table2D out   = y;
        out.caption       = diff_paragraph(x.caption, y.caption, options);
        out.column1       = diff_paragraph(x.column1, y.column1, options);
        out.column2       = diff_paragraph(x.column2, y.column2, options);
        const auto script = edit_script(x.rows, y.rows, [&](const ir::Table2DRow& r, const ir::Table2DRow& s) {
            return similar_paragraphs(r.header, s.header, options);
        });
        out.rows          = script | std::views::transform([&](const Edit& e) {
                       if (e.op == Op::Keep)
                           return ir::Table2DRow{diff_paragraph(x.rows[e.a].header, y.rows[e.b].header, options),
                                                 diff_paragraph(x.rows[e.a].cell1, y.rows[e.b].cell1, options),
                                                 diff_paragraph(x.rows[e.a].cell2, y.rows[e.b].cell2, options)};
                       const ir::Change change = e.op == Op::Delete ? ir::Change::Removed : ir::Change::Added;
                       const auto&      row    = e.op == Op::Delete ? x.rows[e.a] : y.rows[e.b];
                       return ir::Table2DRow{marked_paragraph(row.header, change),
                                             marked_paragraph(row.cell1, change),
                                             marked_paragraph(row.cell2, change)};
                            }) |
                            std::ranges::to<std::vector>();
        out.change        = ir::Change::Same;
        return out;
    }
    Block operator()(const ir::Table1D& x) const {
        const auto& y     = std::get<ir::Table1D>(other);
        ir::Table1D out   = y;
        out.caption       = diff_paragraph(x.caption, y.caption, options);
        out.column1       = diff_paragraph(x.column1, y.column1, options);
        out.column2       = diff_paragraph(x.column2, y.column2, options);
        const auto script = edit_script(x.rows, y.rows, [&](const ir::Table1DRow& r, const ir::Table1DRow& s) {
            return similar_paragraphs(r.cell1, s.cell1, options);
        });
        out.rows          = script | std::views::transform([&](const Edit& e) {
                       if (e.op == Op::Keep)
                           return ir::Table1DRow{diff_paragraph(x.rows[e.a].cell1, y.rows[e.b].cell1, options),
                                                 diff_paragraph(x.rows[e.a].cell2, y.rows[e.b].cell2, options)};
                       const ir::Change change = e.op == Op::Delete ? ir::Change::Removed : ir::Change::Added;
                       const auto&      row    = e.op == Op::Delete ? x.rows[e.a] : y.rows[e.b];
                       return ir::Table1DRow{marked_paragraph(row.cell1, change), marked_paragraph(row.cell2, change)};
                            }) |
                            std::ranges::to<std::vector>();
        out.change        = ir::Change::Same;
        return out;
    }
    Block operator()(const ir::EquivalentTo& x) const {
        return ir::EquivalentTo{diff_code(x.code, std::get<ir::EquivalentTo>(other).code, options)};
    }
    Block operator()(const ir::AuthoredCode& x) const {
        return ir::AuthoredCode{diff_code(x.code, std::get<ir::AuthoredCode>(other).code, options)};
    }
    Block operator()(const ir::Note& x) const {
        return ir::Note{diff_blocks(x.body, std::get<ir::Note>(other).body, options)};
    }
    Block operator()(const ir::Example& x) const {
        return ir::Example{diff_blocks(x.body, std::get<ir::Example>(other).body, options)};
    }
};

// A block with its mark replaced, whichever kind it is.
template <class Block>
struct WithBlockChange {
    ir::Change change;

    template <class T>
    Block operator()(T block) const {
        block.change = change;
        return block;
    }
};

template <class Block>
Block with_block_change(Block block, ir::Change change) {
    return std::visit(WithBlockChange<Block>{change}, std::move(block));
}

template <class Block>
std::vector<Block> diff_blocks(const std::vector<Block>& a, const std::vector<Block>& b, const Options& options) {
    if (a == b)
        return b;
    // A lone block replaced by a lone block of its kind is that block edited,
    // however thorough the edit: the alternative strands an element's label
    // on a struck paragraph and leaves the new one unlabelled.
    const bool              lone_pair = a.size() == 1 && b.size() == 1 && a.front().index() == b.front().index();
    const std::vector<Edit> script =
        edit_script(a, b, [&](const Block& x, const Block& y) { return lone_pair || pairable(x, y, options); });
    return script | std::views::transform([&](const Edit& e) {
               if (e.op == Op::Keep)
                   return std::visit(overloaded{BlockDiffer<Block>{b[e.b], options}}, a[e.a]);
               if (e.op == Op::Delete)
                   return with_block_change(a[e.a], ir::Change::Removed);
               return with_block_change(b[e.b], ir::Change::Added);
           }) |
           std::ranges::to<std::vector>();
}

// --- elements and items --------------------------------------------------------

std::vector<ir::DescriptionElement> diff_elements(const std::vector<ir::DescriptionElement>& a,
                                                  const std::vector<ir::DescriptionElement>& b,
                                                  const Options&                             options) {
    if (a == b)
        return b;
    // The k-th element of a kind pairs with the k-th: `canonicalize` is a
    // stable sort, so the order within a kind is the authored one.
    const std::vector<Edit> script = edit_script(
        a, b, [](const ir::DescriptionElement& x, const ir::DescriptionElement& y) { return x.kind == y.kind; });
    return script | std::views::transform([&](const Edit& e) {
               if (e.op == Op::Keep) {
                   ir::DescriptionElement out = b[e.b];
                   out.body                   = diff_blocks(a[e.a].body, b[e.b].body, options);
                   out.change                 = ir::Change::Same;
                   return out;
               }
               ir::DescriptionElement out = e.op == Op::Delete ? a[e.a] : b[e.b];
               out.change                 = e.op == Op::Delete ? ir::Change::Removed : ir::Change::Added;
               return out;
           }) |
           std::ranges::to<std::vector>();
}

ir::SpecItem diff_item(const ir::SpecItem& a, const ir::SpecItem& b, const Options& options) {
    ir::SpecItem out    = b;
    out.change          = ir::Change::Same;
    out.decl.signatures = diff_code_list(a.decl.signatures, b.decl.signatures, options);
    out.descr.leading   = diff_blocks(a.descr.leading, b.descr.leading, options);
    out.descr.elements  = diff_elements(a.descr.elements, b.descr.elements, options);
    return out;
}

// --- nodes -------------------------------------------------------------------------

std::vector<ir::Node>
diff_nodes(const std::vector<ir::Node>& a, const std::vector<ir::Node>& b, const Options& options);

// Two nodes of one kind paired, diffed inside.
struct NodeDiffer {
    const ir::Node& other;
    const Options&  options;

    ir::Node operator()(const ir::Section& x) const {
        const auto& y = std::get<ir::Section>(other);
        return ir::Section{y.stable_name, y.title, diff_nodes(x.children, y.children, options), ir::Change::Same};
    }
    ir::Node operator()(const ir::Synopsis& x) const {
        const auto&  y   = std::get<ir::Synopsis>(other);
        ir::Synopsis out = y;
        out.code         = diff_code(x.code, y.code, options);
        out.change       = ir::Change::Same;
        return out;
    }
    ir::Node operator()(const ir::SpecItem& x) const { return diff_item(x, std::get<ir::SpecItem>(other), options); }
    ir::Node operator()(const ir::FreeParagraph& x) const {
        return ir::FreeParagraph{diff_paragraph(x.text, std::get<ir::FreeParagraph>(other).text, options),
                                 ir::Change::Same};
    }
};

// What identifies a node among its siblings: an exact key (a section's
// stable name, a synopsis's class, an item's entities and signatures) and,
// for an item, the looser key of its entities alone, so an overload set
// whose signature changed is the same item with a code change.
struct NodeKey {
    std::string exact;
    std::string loose;
    bool        paragraph = false;
};

struct NodeKeyOf {
    NodeKey operator()(const ir::Section& s) const { return {"S:" + s.stable_name, {}, false}; }
    NodeKey operator()(const ir::Synopsis& s) const { return {"Y:" + s.name, {}, false}; }
    NodeKey operator()(const ir::SpecItem& s) const {
        std::vector<std::string> entities = s.decl.entities;
        std::ranges::sort(entities);
        const std::string loose = "E:" + (entities | std::views::join_with(',') | std::ranges::to<std::string>());
        const std::string sigs  = s.decl.signatures | std::views::transform(&ir::CodeText::text) |
                                  std::views::join_with('|') | std::ranges::to<std::string>();
        return {"I:" + loose + "|" + sigs, loose, false};
    }
    NodeKey operator()(const ir::FreeParagraph&) const { return {{}, {}, true}; }
};

std::vector<ir::Node>
diff_nodes(const std::vector<ir::Node>& a, const std::vector<ir::Node>& b, const Options& options) {
    if (a == b)
        return b;
    const auto                 key_of = [](const ir::Node& n) { return std::visit(overloaded{NodeKeyOf{}}, n); };
    const std::vector<NodeKey> ka     = a | std::views::transform(key_of) | std::ranges::to<std::vector>();
    const std::vector<NodeKey> kb     = b | std::views::transform(key_of) | std::ranges::to<std::vector>();

    constexpr std::size_t    unpaired = static_cast<std::size_t>(-1);
    std::vector<std::size_t> pair_a(a.size(), unpaired);
    std::vector<std::size_t> pair_b(b.size(), unpaired);
    const auto               pair_by = [&](auto&& matches) {
        // substrate generic algorithm: a scatter -- each unpaired baseline
        // node claims the first unpaired current node it matches, writing
        // into both tables.
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (pair_a[i] != unpaired)
                continue;
            // substrate generic algorithm: the scatter's inner search for the
            // first unpaired match, which writes into both tables.
            for (std::size_t j = 0; j < b.size(); ++j) {
                if (pair_b[j] == unpaired && matches(i, j)) {
                    pair_a[i] = j;
                    pair_b[j] = i;
                    break;
                }
            }
        }
    };
    pair_by([&](std::size_t i, std::size_t j) { return !ka[i].exact.empty() && ka[i].exact == kb[j].exact; });
    pair_by([&](std::size_t i, std::size_t j) { return !ka[i].loose.empty() && ka[i].loose == kb[j].loose; });
    pair_by([&](std::size_t i, std::size_t j) {
        return ka[i].paragraph && kb[j].paragraph &&
               similar_paragraphs(
                   std::get<ir::FreeParagraph>(a[i]).text, std::get<ir::FreeParagraph>(b[j]).text, options);
    });

    const auto              ia     = std::views::iota(std::size_t{0}, a.size()) | std::ranges::to<std::vector>();
    const auto              ib     = std::views::iota(std::size_t{0}, b.size()) | std::ranges::to<std::vector>();
    const std::vector<Edit> script = edit_script(ia, ib, [&](std::size_t i, std::size_t j) { return pair_a[i] == j; });
    return script | std::views::transform([&](const Edit& e) {
               if (e.op == Op::Keep)
                   return std::visit(overloaded{NodeDiffer{b[e.b], options}}, a[e.a]);
               if (e.op == Op::Delete)
                   return ir::with_change(a[e.a], ir::Change::Removed);
               return ir::with_change(b[e.b], ir::Change::Added);
           }) |
           std::ranges::to<std::vector>();
}

// --- has a change anywhere -------------------------------------------------------

bool paragraph_changed(const ir::Paragraph& p) {
    return std::ranges::any_of(p, [](const ir::Inline& i) { return std::holds_alternative<ir::ChangedInline>(i); });
}
bool code_changed(const ir::CodeText& c) { return !c.changes.empty(); }

template <class Block>
bool blocks_changed(const std::vector<Block>& body);

struct BlockChanged {
    bool operator()(const ir::BodyParagraph& b) const {
        return b.change != ir::Change::Same || paragraph_changed(b.text);
    }
    bool operator()(const ir::Itemize& b) const {
        return b.change != ir::Change::Same || std::ranges::any_of(b.items, paragraph_changed);
    }
    bool operator()(const ir::Table2D& b) const {
        return b.change != ir::Change::Same || paragraph_changed(b.caption) || paragraph_changed(b.column1) ||
               paragraph_changed(b.column2) || std::ranges::any_of(b.rows, [](const ir::Table2DRow& r) {
                   return paragraph_changed(r.header) || paragraph_changed(r.cell1) || paragraph_changed(r.cell2);
               });
    }
    bool operator()(const ir::Table1D& b) const {
        return b.change != ir::Change::Same || paragraph_changed(b.caption) || paragraph_changed(b.column1) ||
               paragraph_changed(b.column2) || std::ranges::any_of(b.rows, [](const ir::Table1DRow& r) {
                   return paragraph_changed(r.cell1) || paragraph_changed(r.cell2);
               });
    }
    bool operator()(const ir::EquivalentTo& b) const { return b.change != ir::Change::Same || code_changed(b.code); }
    bool operator()(const ir::AuthoredCode& b) const { return b.change != ir::Change::Same || code_changed(b.code); }
    bool operator()(const ir::Note& b) const { return b.change != ir::Change::Same || blocks_changed(b.body); }
    bool operator()(const ir::Example& b) const { return b.change != ir::Change::Same || blocks_changed(b.body); }
};

template <class Block>
bool blocks_changed(const std::vector<Block>& body) {
    return std::ranges::any_of(body, [](const Block& b) { return std::visit(overloaded{BlockChanged{}}, b); });
}

bool node_changed(const ir::Node& node);

struct NodeChanged {
    bool operator()(const ir::Section& s) const {
        return s.change != ir::Change::Same || std::ranges::any_of(s.children, node_changed);
    }
    bool operator()(const ir::Synopsis& s) const { return s.change != ir::Change::Same || code_changed(s.code); }
    bool operator()(const ir::SpecItem& s) const {
        return s.change != ir::Change::Same || std::ranges::any_of(s.decl.signatures, code_changed) ||
               blocks_changed(s.descr.leading) ||
               std::ranges::any_of(s.descr.elements, [](const ir::DescriptionElement& e) {
                   return e.change != ir::Change::Same || blocks_changed(e.body);
               });
    }
    bool operator()(const ir::FreeParagraph& s) const {
        return s.change != ir::Change::Same || paragraph_changed(s.text);
    }
};

bool node_changed(const ir::Node& node) { return std::visit(overloaded{NodeChanged{}}, node); }

// --- context -----------------------------------------------------------------------

// Inside a changed section, each maximal run of unchanged children collapses
// to one elided paragraph; a changed child section is treated the same way
// inside.
ir::Node elide_unchanged(ir::Node node) {
    auto* section = std::get_if<ir::Section>(&node);
    if (section == nullptr)
        return node;
    section->children =
        std::move(section->children) | std::views::as_rvalue |
        std::views::chunk_by([](const ir::Node& x, const ir::Node& y) { return node_changed(x) == node_changed(y); }) |
        std::views::transform([](auto&& chunk) -> std::vector<ir::Node> {
            if (!node_changed(*chunk.begin()))
                return {ir::FreeParagraph{{}, ir::Change::Elided}};
            return chunk | std::views::transform([](ir::Node child) { return elide_unchanged(std::move(child)); }) |
                   std::ranges::to<std::vector>();
        }) |
        std::views::join | std::ranges::to<std::vector>();
    return node;
}

std::vector<ir::Node> apply_context(std::vector<ir::Node> nodes, Context context) {
    return foundation::match(context,
                             foundation::when<Context::All>([&] { return std::move(nodes); }),
                             foundation::when<Context::Changed>([&] {
                                 return std::move(nodes) | std::views::as_rvalue | std::views::filter(node_changed) |
                                        std::ranges::to<std::vector>();
                             }),
                             foundation::when<Context::Items>([&] {
                                 return std::move(nodes) | std::views::as_rvalue | std::views::filter(node_changed) |
                                        std::views::transform(
                                            [](ir::Node node) { return elide_unchanged(std::move(node)); }) |
                                        std::ranges::to<std::vector>();
                             }));
}

} // namespace

std::string_view context_name(Context c) { return kContextNames[c]; }

std::optional<Context> context_from_name(std::string_view name) {
    const auto found =
        std::ranges::find_if(foundation::enumerators<Context>, [&](Context c) { return kContextNames[c] == name; });
    return found != foundation::enumerators<Context>.end() ? std::optional<Context>{*found} : std::nullopt;
}

ir::Document compare(const ir::Document& baseline, const ir::Document& current, const Options& options) {
    ir::Document out = current;
    out.nodes        = apply_context(diff_nodes(baseline.nodes, current.nodes, options), options.context);
    return out;
}

bool has_marks(const ir::Document& document) { return std::ranges::any_of(document.nodes, node_changed); }

} // namespace beman::specgen::diff
