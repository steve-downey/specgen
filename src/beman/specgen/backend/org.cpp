// src/beman/specgen/backend/org.cpp                                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The org-mode backend, written as a direct algebra over
// backend::backend::RenderF -- the third file in this directory with that
// shape, after latex.cpp and mpark.cpp. Decision backend-direct-algebra's
// claim that `RenderF` / `RenderedSectionF` / `render_fmap` are a substrate
// holds across all three backends; nothing here needs to change them either.
//
// Design §8 puts this backend in a different position from the other two:
// "escape convention negotiated with the orgwg21 exporter ... correctness is
// defined by the exporter". The exporter is `wg21org` (ox-wg21latex.el for the
// authoritative PDF path, ox-wg21html.el beside it), and the four conventions
// below are settled against it rather than chosen here.
//
//   1. Code goes in **special blocks, not src blocks**. `#+begin_codeblock`
//      and `#+begin_itemdecl` are exported by org's stock
//      `org-latex-special-block` as `\begin{codeblock}` / `\begin{itemdecl}`,
//      which are the draft's own `lstnewenvironment`s -- they reach a wg21org
//      paper through `common.tex`'s `\input{stdtex/macros}`. Using
//      `#+begin_src c++` instead would have bought keyword fontification and
//      cost all parsing freedom, since a src block's text is the exporter's
//      to route rather than to read.
//
//   2. Which means the span escape *is* the draft's, `@\exposid{value}@`,
//      because inside those environments `escapechar=@` and `texcl=true` are
//      genuinely in force. This backend and the LaTeX one therefore emit the
//      same bytes for a code block's spans, and share the one function that
//      produces them (`backend::draft_span_codeblock`) rather than each
//      spelling the convention out.
//
//   3. Prose is org, not LaTeX: `/Effects/: ` for a description element,
//      `~code~` for a code inline, `([optional.general])` for a
//      cross-reference. `@...@` is a listings option and is inert out here, so
//      a code inline that carries a *span* -- and only such an inline --
//      leaves org for an `@@latex:...@@` export snippet.
//
//   4. Normative paragraphs go in `#+begin_pnum` special blocks, whose wg21org
//      exporters supply the target-specific counter. The top-level generated
//      sections are `:UNNUMBERED:` like mpark's `{-}` headings. Top-level
//      sections also carry `:WG21_WORDING: t`; wg21org uses that property to
//      apply wording paragraph numbering and presentation to exactly the
//      generated subtree, while the including paper still owns its framing.

#include <beman/specgen/backend/org.hpp>

#include <beman/specgen/backend/code_spans.hpp>
#include <beman/specgen/backend/draft_spans.hpp>
#include <beman/specgen/backend/element_label.hpp>
#include <beman/specgen/backend/render_fold.hpp>
#include <beman/specgen/backend/section_title.hpp>
#include <beman/specgen/foundation/enumeration.hpp>
#include <beman/specgen/foundation/overloaded.hpp>
#include <beman/specgen/ir.hpp>

#include <beman/tree_algorithms/recursion_schemes.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace beman::specgen::backend::org {

namespace {

using beman::specgen::foundation::overloaded;

// --- code -------------------------------------------------------------------

// The span escape is `backend::draft_span_codeblock` -- the draft's own, not
// one of this backend's -- for the reason in note 2 at the top of this file.
// It is passed to the same `render_code_spans` extension point every backend
// plugs into (decision backend-direct-algebra).
std::string escape_span(const ir::Span& span, std::string_view spelling) {
    if (span.kind == ir::SpanKind::LibraryIndex)
        return std::string(spelling); // Paper fragments do not build an index.
    return backend::draft_span_codeblock(span, spelling);
}

// A span inside a changed run of code: the run has opened the `@...@`
// escape already, so the span takes the draft's escaped form -- the same
// bytes the LaTeX backend writes there, less the index it never builds.
std::string escape_span_in_run(const ir::Span& span, std::string_view spelling) {
    if (span.kind == ir::SpanKind::LibraryIndex)
        return std::string(spelling);
    return backend::draft_span_escaped(span, spelling);
}

std::string render_code(const ir::CodeText& code) {
    return backend::render_code_spans(code, escape_span, escape_span_in_run, backend::draft_change_codeblock);
}

// One special block. `env` is the draft environment name org will export to,
// which is also the org block name -- that identity is the whole point of
// using a special block, and is why this takes the name rather than deciding
// it.
std::string render_block(std::string_view env, const ir::CodeText& code) {
    return std::format("#+begin_{}\n{}\n#+end_{}\n", env, render_code(code), env);
}

std::string wrap_pnum(std::string text) {
    return "#+begin_pnum\n" + text + (text.ends_with('\n') ? "" : "\n") + "#+end_pnum\n";
}

// A marked node or element in the special block of the draft environment's
// name, `addedblock` / `removedblock`, which org's stock export turns into
// that environment (docs/plans/wording-diff.md); an unmarked one as it is.
std::string wrap_block(ir::Change change, std::string text) {
    const std::optional<std::string_view> environment = backend::draft_change_environment(change);
    if (!environment)
        return text;
    return std::format(
        "#+begin_{}\n{}{}#+end_{}\n", *environment, text, text.ends_with('\n') ? "" : "\n", *environment);
}

// Under the wording diff only an *added* paragraph takes the `x`, `x+1`
// literal label: a retained or removed one keeps the counter wg21org gives
// it. Which paragraphs are added is read back off the rendered text, as the
// mpark backend reads its divs: a `#+begin_pnum` is added while an
// `addedblock` is open, or under a heading whose properties say
// `:WG21_CHANGE: add` -- until the next heading at that level or shallower.
// One ascending run over the whole document.
std::string number_added_pnums(const std::string& text) {
    unsigned    count        = 0;
    int         block_depth  = 0;
    std::size_t added_level  = 0; // the level of the open added heading, 0 for none
    std::size_t last_heading = 0;
    std::string out;
    out.reserve(text.size());
    const bool             trailing_newline = text.ends_with('\n');
    const std::string_view body = std::string_view(text).substr(0, text.size() - (trailing_newline ? 1 : 0));
    // substrate generic algorithm: a line walk whose state is the open blocks,
    // the open heading and the paragraph count together -- a fold with
    // several accumulators, written as the loop it is.
    for (auto&& piece : body | std::views::split('\n')) {
        const std::string_view line(piece.begin(), piece.end());
        const std::size_t      stars = line.find_first_not_of('*');
        if (line.starts_with("#+begin_addedblock")) {
            ++block_depth;
        } else if (line.starts_with("#+end_addedblock")) {
            block_depth = std::max(0, block_depth - 1);
        } else if (stars != 0 && stars != std::string_view::npos && line[stars] == ' ') {
            last_heading = stars;
            if (added_level != 0 && stars <= added_level)
                added_level = 0;
        } else if (line == ":WG21_CHANGE: add" && added_level == 0) {
            added_level = last_heading;
        }
        const bool added = block_depth > 0 || added_level != 0;
        if (added && line == "#+begin_pnum") {
            out += std::format("#+begin_pnum {}", count == 0 ? std::string{"x"} : std::format("x+{}", count));
            ++count;
        } else {
            out += line;
        }
        out += '\n';
    }
    if (!trailing_newline && !out.empty())
        out.pop_back();
    return out;
}

// --- prose ------------------------------------------------------------------

// Org's code markup is `~x~`, and org has no escape for a literal `~` inside
// it: `~~optional()~` closes the run at the second character. C++ spells two
// real things with that character (a destructor name, bitwise complement), so
// this is a shape that reaches wording rather than a theoretical one. There is
// no org spelling that survives it, so such an inline takes the same route a
// spanned one takes -- an export snippet -- which costs nothing, since the
// branch already exists.
bool needs_latex_snippet(const ir::CodeInline& v) {
    return !v.code.spans.empty() || v.code.text.contains('~') || v.code.text.empty();
}

std::string html_escape(std::string_view text) {
    return text | std::views::transform([](const char ch) {
               switch (ch) {
               case '&':
                   return std::string{"&amp;"};
               case '<':
                   return std::string{"&lt;"};
               case '>':
                   return std::string{"&gt;"};
               default:
                   return std::string(1, ch);
               }
           }) |
           std::views::join | std::ranges::to<std::string>();
}

std::string html_span(const ir::Span& span, std::string_view spelling) {
    return foundation::match(
        span.kind,
        foundation::when<ir::SpanKind::ExposId>([&] { return "<var>" + html_escape(span.payload) + "</var>"; }),
        foundation::when<ir::SpanKind::SeeBelow>([&] { return "<var>see below</var>"; }),
        foundation::when<ir::SpanKind::ImplDefined>([&] { return "<var>implementation-defined</var>"; }),
        foundation::when<ir::SpanKind::Placeholder>([&] { return "<var>" + html_escape(span.payload) + "</var>"; }),
        foundation::when<ir::SpanKind::Ref>([&] { return "[" + html_escape(span.payload) + "]"; }),
        foundation::when<ir::SpanKind::LibraryIndex>([&] { return html_escape(spelling); }));
}

std::string html_code_inline(const ir::CodeInline& v) {
    struct State {
        std::string out;
        std::size_t pos = 0;
    };
    const std::string_view text = v.code.text;
    State state = std::ranges::fold_left(v.code.spans, State{}, [text](State state, const ir::Span& span) {
        if (span.begin <= text.size() && span.end <= text.size() && span.begin >= state.pos &&
            span.end >= span.begin) {
            state.out += html_escape(text.substr(state.pos, span.begin - state.pos));
            state.out += html_span(span, text.substr(span.begin, span.end - span.begin));
            state.pos = span.end;
        }
        return state;
    });
    state.out += html_escape(text.substr(state.pos));
    return "<code>" + state.out + "</code>";
}

bool under_new_root(std::string_view name, std::span<const std::string> new_roots) {
    return std::ranges::any_of(new_roots, [name](std::string_view root) {
        return name == root || (name.starts_with(root) && name.size() > root.size() && name[root.size()] == '.');
    });
}

// Dispatches one Inline (a Paragraph's Piece) to its org rendering. Four
// alternatives (decision visitation-rules' >3 rule), so a named visitor
// struct, mirroring the PieceRenderer in each of the other two backends.
struct PieceRenderer {
    std::span<const std::string> new_roots;

    // Plain prose text, verbatim -- the same treatment the other two backends
    // give it, and with the same justification: the author's prose is the
    // author's. Note that org is the most active of the three targets here
    // (`_` and `^` are subscript/superscript under `#+options: ^:t`, `*` and
    // `/` are emphasis), so prose that needs escaping fails more visibly than
    // it would elsewhere. Left as pass-through deliberately: identifiers reach
    // wording inside a code inline, where they are protected, and an escaper
    // guessing at authored prose would corrupt more than it saved.
    std::string operator()(const ir::TextInline& v) const { return v.text; }

    // A code inline. Org-native when org can express it, and a LaTeX export
    // snippet when it cannot -- which is a span (org's code markup is
    // verbatim, so `~x.$val$~` has no meaning) or a literal `~`. The snippet's
    // payload is the draft's own rendering, shared with the LaTeX backend, so
    // a whole-span inline is the bare `\exposid{value}` there too rather than
    // a doubled-up `\tcode{\exposid{value}}`.
    std::string operator()(const ir::CodeInline& v) const {
        if (needs_latex_snippet(v))
            return "@@latex:" + backend::draft_code_inline(v) + "@@@@html:" + html_code_inline(v) + "@@";
        return '~' + v.code.text + '~';
    }

    // A cross-reference to another stable name.  Build the link here, while
    // the IR still distinguishes it from authored prose and C++ punctuation.
    std::string operator()(const ir::RefInline& v) const {
        const std::string target =
            (under_new_root(v.stable_name, new_roots) ? "#" : "https://eel.is/c++draft/") + v.stable_name;
        return "([[" + target + "][@@html:[" + v.stable_name + "]@@@@latex:{[}" + v.stable_name + "{]}@@]])";
    }

    // A library concept name. The draft's \libconcept sets the code font and
    // links; org has only the code font to offer, which is what `~...~` gives
    // -- the same trade the mpark backend makes with backticks.
    std::string operator()(const ir::ConceptRef& v) const { return '~' + v.name + '~'; }

    // A changed run of prose (the wording diff's mark): wg21org's
    // `[[insert:][...]]` / `[[delete:][...]]` links around the run. Defined
    // after `RunPieceRenderer` below, which renders the run's contents.
    std::string operator()(const ir::ChangedInline& run) const;
};

// The contents of a changed run, which is a link's description, where org
// allows every inline but another link. A cross-reference inside a run is
// therefore the `{{{sref(name)}}}` macro rather than the link PieceRenderer
// writes, and wg21org expands the macro to the same target the link resolves
// to (docs/plans/wording-diff.md, org-forms). The other three cases are
// PieceRenderer's own: an export snippet and `~code~` are both allowed in a
// description. Four alternatives of the non-recursive `PlainInline`, so a
// named visitor of its own.
struct RunPieceRenderer {
    std::string operator()(const ir::TextInline& v) const { return PieceRenderer{}(v); }
    std::string operator()(const ir::CodeInline& v) const { return PieceRenderer{}(v); }
    std::string operator()(const ir::RefInline& v) const { return "{{{sref(" + v.stable_name + ")}}}"; }
    std::string operator()(const ir::ConceptRef& v) const { return PieceRenderer{}(v); }
};

std::string render_run(const std::vector<ir::PlainInline>& inlines) {
    return inlines | std::views::transform([](const ir::PlainInline& piece) {
               return std::visit(overloaded{RunPieceRenderer{}}, piece);
           }) |
           std::views::join | std::ranges::to<std::string>();
}

std::string PieceRenderer::operator()(const ir::ChangedInline& run) const {
    const std::string inner = render_run(run.inlines);
    return foundation::match(run.change,
                             foundation::when<ir::Change::Same>([&] { return inner; }),
                             foundation::when<ir::Change::Added>([&] { return "[[insert:][" + inner + "]]"; }),
                             foundation::when<ir::Change::Removed>([&] { return "[[delete:][" + inner + "]]"; }));
}

std::string render_paragraph(const ir::Paragraph& para, std::span<const std::string> new_roots) {
    return para | std::views::transform([new_roots](const ir::Inline& piece) {
               return std::visit(overloaded{PieceRenderer{new_roots}}, piece);
           }) |
           std::views::join | std::ranges::to<std::string>();
}

std::string render_table_cell(const ir::Paragraph& paragraph, std::span<const std::string> new_roots) {
    return render_paragraph(paragraph, new_roots) | std::views::transform([](const char ch) {
               return ch == '|' ? std::string{"\\vert{}"} : std::string(1, ch);
           }) |
           std::views::join | std::ranges::to<std::string>();
}

std::string render_table(const ir::Table2D& table, std::span<const std::string> new_roots) {
    std::string out = std::format("#+name: {}\n#+caption: {}\n"
                                  "#+ATTR_WG21: :columns 18 36 36\n"
                                  "| | {} | {} |\n|-\n",
                                  table.stable_name,
                                  render_paragraph(table.caption, new_roots),
                                  render_table_cell(table.column1, new_roots),
                                  render_table_cell(table.column2, new_roots));
    out += table.rows | std::views::transform([new_roots](const ir::Table2DRow& row) {
               return std::format("| {} | {} | {} |\n",
                                  render_table_cell(row.header, new_roots),
                                  render_table_cell(row.cell1, new_roots),
                                  render_table_cell(row.cell2, new_roots));
           }) |
           std::views::join | std::ranges::to<std::string>();
    return out;
}

// \libtab2's flat two-column table (issue #74): a named, captioned org table
// with no row-heading column to spare.
std::string render_flat_table(const ir::Table1D& table, std::span<const std::string> new_roots) {
    std::string out = std::format("#+name: {}\n#+caption: {}\n"
                                  "#+ATTR_WG21: :columns 25 67\n"
                                  "| {} | {} |\n|-\n",
                                  table.stable_name,
                                  render_paragraph(table.caption, new_roots),
                                  render_table_cell(table.column1, new_roots),
                                  render_table_cell(table.column2, new_roots));
    out += table.rows | std::views::transform([new_roots](const ir::Table1DRow& row) {
               return std::format(
                   "| {} | {} |\n", render_table_cell(row.cell1, new_roots), render_table_cell(row.cell2, new_roots));
           }) |
           std::views::join | std::ranges::to<std::string>();
    return out;
}

// --- items --------------------------------------------------------------

// One DescriptionElement's paragraphs, in emission order -- the counterpart of
// the same function in each of the other two backends, block for block. The
// caller joins these, and every other element's, with a blank-line separator.
//
// The element's label ("/Effects/: ") comes from backend::element_label and
// appears exactly once, on whichever block is emitted first. Italic rather
// than bold because that is what the draft's `\Fundesc` sets and what
// `view-maybe.org` writes by hand; there is no paragraph number in front of it
// (note 4 at the top of this file).
// One block's rendering and paragraph-boundary policy. Lists and tables
// belong to the preceding numbered paragraph; an extracted body starts one.
struct RenderedBodyBlock {
    std::string text;
    bool        starts_paragraph = false;
    bool        inline_label     = false;
};

struct LeafBodyRenderer {
    std::span<const std::string> new_roots;
    // Authored prose retains its paragraph boundary or explicit continuation.
    RenderedBodyBlock operator()(const ir::BodyParagraph& p) const;
    // Itemizations stay in the paragraph they enumerate.
    RenderedBodyBlock operator()(const ir::Itemize& list) const;
    // Every authored table remains in its source position.
    RenderedBodyBlock operator()(const ir::Table2D& t) const;
    RenderedBodyBlock operator()(const ir::Table1D& t) const;
    // Extraction retains its semantic introduction and its own paragraph.
    RenderedBodyBlock operator()(const ir::EquivalentTo& e) const;
    RenderedBodyBlock operator()(const ir::AuthoredCode& code) const;
};

RenderedBodyBlock LeafBodyRenderer::operator()(const ir::BodyParagraph& p) const {
    return {render_paragraph(p.text, new_roots) + '\n', !p.continuation, true};
}

RenderedBodyBlock LeafBodyRenderer::operator()(const ir::Itemize& list) const {
    const std::string items = list.items | std::views::transform([this](const ir::Paragraph& p) {
                                  return "- " + render_paragraph(p, new_roots) + '\n';
                              }) |
                              std::views::join | std::ranges::to<std::string>();
    return {items, false, false};
}

RenderedBodyBlock LeafBodyRenderer::operator()(const ir::Table2D& t) const {
    return {render_table(t, new_roots), false, false};
}

RenderedBodyBlock LeafBodyRenderer::operator()(const ir::Table1D& t) const {
    return {render_flat_table(t, new_roots), false, false};
}

RenderedBodyBlock LeafBodyRenderer::operator()(const ir::EquivalentTo& e) const {
    return {"Equivalent to:\n\n" + render_block("codeblock", e.code), true, true};
}

RenderedBodyBlock LeafBodyRenderer::operator()(const ir::AuthoredCode& code) const {
    return {render_block("codeblock", code.code), false, false};
}

std::string render_informative_body(const ir::InformativeBody& body, std::span<const std::string> new_roots);
std::string render_informative_body(const ir::InformativeBody& body, std::span<const std::string> new_roots) {
    return body | std::views::transform([new_roots](const ir::InformativeBlock& block) {
               return std::visit(overloaded{LeafBodyRenderer{new_roots}}, block).text;
           }) |
           std::views::join_with('\n') | std::ranges::to<std::string>();
}

struct BodyRenderer : LeafBodyRenderer {
    using LeafBodyRenderer::operator();
    RenderedBodyBlock operator()(const ir::Note& note) const;
    RenderedBodyBlock operator()(const ir::Example& example) const;
};

RenderedBodyBlock BodyRenderer::operator()(const ir::Note& note) const {
    return {std::format("#+begin_note\n{}#+end_note\n", render_informative_body(note.body, new_roots)), false, false};
}

RenderedBodyBlock BodyRenderer::operator()(const ir::Example& example) const {
    return {std::format("#+begin_wgexample\n{}#+end_wgexample\n", render_informative_body(example.body, new_roots)),
            false,
            false};
}

std::vector<std::string> body_blocks(const ir::DescriptionBody&     body,
                                     std::optional<ir::ElementKind> kind,
                                     std::span<const std::string>   new_roots);

std::vector<std::string> body_blocks(const ir::DescriptionBody&     body,
                                     std::optional<ir::ElementKind> kind,
                                     std::span<const std::string>   new_roots) {
    const std::string label  = kind ? std::format("/{}/", backend::element_label(*kind)) : std::string{};
    auto              blocks = std::ranges::fold_left(
        body, std::vector<std::string>{}, [&](std::vector<std::string> out, const ir::BodyBlock& block) {
            auto       rendered = std::visit(overloaded{BodyRenderer{{new_roots}}}, block);
            const bool first    = out.empty();
            if (first || rendered.starts_paragraph) {
                const std::string prefix =
                    first && kind ? label + (rendered.inline_label ? ": " : ":\n\n") : std::string{};
                out.push_back(prefix + rendered.text);
            } else {
                out.back() += '\n' + rendered.text;
            }
            return out;
        });
    std::ranges::transform(blocks, blocks.begin(), [](std::string b) { return wrap_pnum(std::move(b)); });
    return blocks;
}

std::vector<std::string> element_blocks(const ir::DescriptionElement& element, std::span<const std::string> new_roots);
std::vector<std::string> element_blocks(const ir::DescriptionElement& element,
                                        std::span<const std::string>  new_roots) {
    return body_blocks(element.body, element.kind, new_roots);
}

// Canonicalization orders derived evidence before authored wording. Merge
// complete bodies so a list, continuation, or table never crosses its prose.
ir::DescriptionElement merge_element_group(std::ranges::range auto&& group) {
    ir::DescriptionElement out;
    out.kind = std::ranges::begin(group)->kind;
    out.body = ir::coalesce_itemizations(
        group |
        std::views::transform([](const ir::DescriptionElement& e) -> const ir::DescriptionBody& { return e.body; }) |
        std::views::join | std::ranges::to<ir::DescriptionBody>());
    return out;
}

std::vector<std::string> element_group_blocks(std::ranges::range auto&&    group,
                                              std::span<const std::string> new_roots) {
    return element_blocks(merge_element_group(group), new_roots);
}

std::string render_item(const ir::SpecItem& item, std::span<const std::string> new_roots) {
    // Index entries are dropped (design §8: "draft backend expands, others
    // drop"). `\indexlibrarymember` builds the draft's own index; a paper
    // fragment has none to build. Note this is a *policy* about papers rather
    // than about the target: unlike mpark, this backend could emit the macro
    // and have it work, since the draft's index machinery is loaded.
    // No signatures means a description with no itemdecl -- a class's own
    // wording (design §6), which the draft writes as bare paragraphs in a
    // general subclause. An empty itemdecl block would be the same blank box
    // design §9 rejects on a synopsis.
    std::string out;
    if (!item.decl.signatures.empty()) {
        out = "#+begin_itemdecl\n";
        out += item.decl.signatures |
               std::views::transform([](const ir::CodeText& sig) { return render_code(sig) + '\n'; }) |
               std::views::join | std::ranges::to<std::string>();
        out += "#+end_itemdecl\n";
    }

    if (!beman::specgen::ir::has_description(item.descr))
        return wrap_block(item.change, std::move(out));

    // Grouped by mark as well as kind, each group in its own block when
    // marked -- the same rule, for the same reason, as latex.cpp's render_item.
    std::vector<std::string> groups = body_blocks(item.descr.leading, std::nullopt, new_roots);
    groups.append_range(item.descr.elements |
                        std::views::chunk_by([](const ir::DescriptionElement& a, const ir::DescriptionElement& b) {
                            return a.kind == b.kind && a.change == b.change;
                        }) |
                        std::views::transform([new_roots](auto&& group) {
                            return wrap_block(std::ranges::begin(group)->change,
                                              element_group_blocks(group, new_roots) | std::views::join_with('\n') |
                                                  std::ranges::to<std::string>());
                        }));

    if (!out.empty())
        out += '\n';
    out += groups | std::views::join_with('\n') | std::ranges::to<std::string>();
    return wrap_block(item.change, std::move(out));
}

// --- the direct algebra over backend::backend::RenderF ---------------------

// Inherited attribute carrier, handed down through `project` below. Only the
// outline level travels. Paragraph numbering is delegated to wg21org's pnum
// special block and therefore needs no counter in this rendering context.
struct RenderCtx {
    int  level;
    bool wording_root;
};

// The fold's seed/handle type: a node paired with the RenderCtx its parent
// assigned it, so a Section's own outline level is resolved before the node is
// visited rather than threaded through the algebra's result type.
struct Seeded {
    const ir::Node* node;
    RenderCtx       ctx;
};

// `Seeded`'s std::visit dispatch (decision visitation-rules: named struct,
// ir::Node has four alternatives), member state rather than a capture.
struct SeededProjector {
    RenderCtx ctx;

    // A subsection. The heading depends on *this* node's level, which is known
    // only here during descent, so it is rendered now into
    // `RenderedSectionF::header` -- the field named for exactly this.
    //
    // The stable name rides the heading as bracketed text and as its export
    // target.  The wg21org exporters preserve CUSTOM_ID verbatim for both
    // HTML anchors and LaTeX labels, so no target-specific markup is needed.
    backend::RenderF<Seeded> operator()(const ir::Section& s) const {
        const std::string stars(static_cast<std::size_t>(std::max(ctx.level, 1)), '*');
        // A hand-written Section may carry no title; emitting the empty one
        // would leave a double space before the stable name.
        std::string header =
            s.title.empty()
                ? std::format("{} [{}]\n", stars, s.stable_name)
                : std::format("{} {} [{}]\n", stars, backend::render_section_title(s.title, '~'), s.stable_name);
        // A marked section (the wording diff's) says so in its properties,
        // `:WG21_CHANGE: add` or `rm`, rather than in a block around it: the
        // exporter applies the change to the subtree it owns.
        header += std::format(
            ":PROPERTIES:\n:CUSTOM_ID: {}\n:UNNUMBERED: t\n{}{}:END:\n",
            s.stable_name,
            ctx.wording_root ? ":WG21_WORDING: t\n" : "",
            foundation::match(
                s.change,
                foundation::when<ir::Change::Same>([] { return std::string_view{}; }),
                foundation::when<ir::Change::Added>([] { return std::string_view{":WG21_CHANGE: add\n"}; }),
                foundation::when<ir::Change::Removed>([] { return std::string_view{":WG21_CHANGE: rm\n"}; })));

        const RenderCtx     child_ctx{ctx.level + 1, false};
        std::vector<Seeded> children =
            s.children |
            std::views::transform([&child_ctx](const ir::Node& child) { return Seeded{&child, child_ctx}; }) |
            std::ranges::to<std::vector>();
        return backend::RenderedSectionF<Seeded>{std::move(header), std::move(children), s.change};
    }

    // The three leaves render independently of outline level, so they pass
    // through unchanged.
    backend::RenderF<Seeded> operator()(const ir::Synopsis& v) const { return v; }
    backend::RenderF<Seeded> operator()(const ir::SpecItem& v) const { return v; }
    backend::RenderF<Seeded> operator()(const ir::FreeParagraph& v) const { return v; }
};

backend::RenderF<Seeded> project(const Seeded& seeded) {
    return std::visit(overloaded{SeededProjector{seeded.ctx}}, *seeded.node);
}

// Dispatches one already-rendered RenderF layer to its org text. Four
// alternatives (decision visitation-rules' >3 rule), so a named visitor
// struct. The heading has
// already been resolved by `project`, so every case is a plain
// RenderF<std::string> -> std::string mapping.
struct OrgAlgebra {
    std::span<const std::string> new_roots;

    std::string operator()(const backend::RenderedSectionF<std::string>& s) const {
        if (s.children.empty())
            return s.header;
        return s.header + "\n" + (s.children | std::views::join_with('\n') | std::ranges::to<std::string>());
    }

    // A synopsis is a `codeblock`, an itemdecl's signatures are an `itemdecl`
    // -- the same two environments the LaTeX backend picks, because they are
    // the same two environments.
    // A marked leaf (the wording diff's) closes in its block; a marked
    // section carried its mark on its heading's properties instead.
    std::string operator()(const ir::Synopsis& v) const {
        return wrap_block(v.change, render_block("codeblock", v.code));
    }
    std::string operator()(const ir::SpecItem& v) const { return render_item(v, new_roots); }
    std::string operator()(const ir::FreeParagraph& v) const {
        return wrap_block(v.change, wrap_pnum(render_paragraph(v.text, new_roots) + '\n'));
    }
};

std::string render_layer(const backend::RenderF<std::string>& layer, std::span<const std::string> new_roots) {
    return std::visit(overloaded{OrgAlgebra{new_roots}}, layer);
}

std::string render_node_to_string(const ir::Node& node, const RenderCtx& ctx, std::span<const std::string> new_roots) {
    return beman::tree_algorithms::fold_with<std::string>(
        [new_roots](const backend::RenderF<std::string>& layer) { return render_layer(layer, new_roots); },
        backend::render_fmap,
        project,
        Seeded{&node, ctx});
}

} // namespace

std::string render_to_string(const ir::Document& doc, const Options& options) {
    const RenderCtx ctx{options.base_heading_level, true};
    // Paper mode is the wording diff with an empty baseline
    // (docs/plans/wording-diff.md): every top-level node marked added, and
    // the marks then render exactly as a diff's would. A marked top-level
    // section carries its mark on its heading, through the projector; a
    // marked top-level leaf is rendered unmarked and closed in its block here,
    // one block per node, which is where paper mode has always put it.
    const std::optional<std::vector<ir::Node>> marked =
        options.paper_mode ? std::optional{ir::all_marked(doc.nodes, ir::Change::Added)} : std::nullopt;
    const std::vector<ir::Node>&   nodes = marked ? *marked : doc.nodes;
    const std::vector<std::string> rendered =
        nodes | std::views::transform([&](const ir::Node& node) {
            if (std::holds_alternative<ir::Section>(node))
                return render_node_to_string(node, ctx, options.new_roots);
            return wrap_block(ir::change_of(node),
                              render_node_to_string(ir::with_change(node, ir::Change::Same), ctx, options.new_roots));
        }) |
        std::ranges::to<std::vector>();
    return number_added_pnums(rendered | std::views::join_with('\n') | std::ranges::to<std::string>());
}

std::string render_to_string(const ir::SpecItem& item, const Options& options) {
    const ir::Change change = options.paper_mode ? ir::Change::Added : item.change;
    ir::SpecItem     copy   = item;
    copy.change             = ir::Change::Same;
    return number_added_pnums(wrap_block(change, render_item(copy, options.new_roots)));
}

} // namespace beman::specgen::backend::org
