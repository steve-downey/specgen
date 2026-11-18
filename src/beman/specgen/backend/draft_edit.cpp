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
std::optional<std::string> listing_text(const ir::CodeText& code, std::size_t begin, std::size_t end) {
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
        } else if (a0 < dt.size()) {
            begin = end = dt[a0].begin;
            replacement = *added + (paper_space(b1) || b1 == pt.size() ? " " : "");
        } else {
            begin = end = dt.empty() ? 0 : dt.back().end;
            replacement = (paper_space(b0) ? " " : "") + *added;
        }
        out.edits[source->file].push_back(
            {file_at(begin), begin == end ? file_at(begin) : file_end(end), replacement});
    }
}

// --- placing whole nodes and descriptions (issue #346) ----------------------------

// A node as the LaTeX backend writes it.
std::string latex_of(const ir::Node& node) {
    ir::Document document;
    document.nodes.push_back(node);
    return latex::render_to_string(document);
}

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
            out.edits[range->file].push_back({offset, offset + lines[k].size(), draft_layout(mine[k])});
        offset += lines[k].size();
    }
}

// The edit that puts `node`, which the paper adds, after `anchor`'s place in
// the draft's source, or before it when `before` is set. A blank line
// separates it from its neighbour, as the draft's items are separated.
bool place_added(const ir::Node& node, const ir::NodeOrigin& anchor, bool before, Collector& out) {
    const ir::SourceRange* range = anchor.range.get();
    if (range == nullptr || std::holds_alternative<ir::Section>(node))
        return false;
    std::string written = draft_layout(latex_of(node));
    if (!written.ends_with('\n'))
        written += '\n';
    if (before)
        out.edits[range->file].push_back({range->begin, range->begin, written + '\n'});
    else
        out.edits[range->file].push_back({range->end, range->end, '\n' + written});
    return true;
}

// The edit that takes a node the paper removes out of the draft's source,
// with the blank line after it.
bool place_removed(const ir::NodeOrigin& origin, Collector& out) {
    const ir::SourceRange* range = origin.range.get();
    if (range == nullptr)
        return false;
    std::size_t end  = range->end;
    const auto  text = out.text_of(range->file);
    if (text && end < text->size() && (*text)[end] == '\n')
        ++end;
    out.edits[range->file].push_back({range->begin, end, {}});
    return true;
}

// Where a node was read from, when it says.
ir::NodeOrigin origin_of(const ir::Node& node) {
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
                 Collector&                   out);

void place_item(const ir::SpecItem& draft, const ir::SpecItem& paper, const std::string& where, Collector& out) {
    const std::string here =
        paper.decl.signatures.empty() ? where : std::format("{}: `{}`", where, paper.decl.signatures.front().text);
    // Each paper signature with the draft signature that reads most like it.
    std::vector<bool> claimed(draft.decl.signatures.size(), false);
    // substrate generic algorithm: each paper signature claims its draft one.
    for (const ir::CodeText& signature : paper.decl.signatures) {
        std::optional<std::size_t> best;
        double                     best_score = -1.0;
        // substrate generic algorithm: an argmax over the unclaimed draft signatures.
        for (std::size_t d = 0; d < draft.decl.signatures.size(); ++d) {
            const double s = alike(draft.decl.signatures[d], signature);
            if (!claimed[d] && s > best_score) {
                best       = d;
                best_score = s;
            }
        }
        if (!best) {
            out.error(here, "the paper adds a declaration to this item, which cannot be placed yet");
            continue;
        }
        claimed[*best] = true;
        place_code(draft.decl.signatures[*best], signature, here, out);
    }
    if (std::ranges::any_of(claimed, std::logical_not<>{}))
        out.error(here, "the paper removes a declaration from this item, which cannot be placed yet");
    if (wording_differs(draft, paper))
        place_description(draft, paper, here, out);
}

void place_node(const ir::Node& draft, const ir::Node& paper, const std::string& where, Collector& out) {
    if (const auto* section = std::get_if<ir::Section>(&paper)) {
        place_nodes(std::get<ir::Section>(draft).children, section->children, section->stable_name, out);
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
                 Collector&                   out) {
    const std::vector<std::optional<std::size_t>> pairs = pair_nodes(draft, paper);
    std::vector<bool>                             used(draft.size(), false);
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
            if (!place_added(paper[p], anchor, before, out))
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
    place_nodes(draft.nodes, sections, {}, out);
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
