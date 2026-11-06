// src/beman/specgen/backend/mpark.cpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The mpark/wg21 backend, written as a direct algebra over
// backend::backend::RenderF -- the same shape backend/latex.cpp uses, and
// deliberately so. Decision backend-direct-algebra calls
// `RenderF`/`RenderedSectionF`/`render_fmap` a shared substrate,
// and the parallel between the two files is the evidence for that claim. Where this
// backend departs from the LaTeX one, the divergence is a property of the
// target syntax and is commented at the point it happens.
//
// Three such departures are worth knowing before reading:
//
//   1. One escape function, not two. LaTeX needs `\exposid{x}` in prose and
//      `@\exposid{x}@` inside a codeblock, because a codeblock is verbatim and
//      has to escape *back* into LaTeX. mpark's embedded Markdown is already
//      on inside both a ```cpp fence and an inline `code` span, so the same
//      `$x$` works in both and `escape_span` has no context parameter.
//
//   2. No whole-span special case. `latex.cpp`'s PieceRenderer emits a bare
//      macro for a CodeInline that is entirely one span, because \exposid and
//      \seebelow already set the code font and \tcode would double up. Here
//      the backticks *are* the code font and `$...$` only adds the italics, so
//      there is nothing to double and every CodeInline is backticked.
//
//   3. Paragraph numbers are not counted. `[#]{.pnum}` is the framework's
//      auto-numbering form, which is the exact analogue of LaTeX's
//      self-numbering `\pnum`: this backend emits the same token for every
//      paragraph and the pandoc filter assigns the numbers. That is what keeps
//      regeneration idempotent, and it is why `RenderCtx` carries no counter.

#include <beman/specgen/backend/mpark.hpp>

#include <beman/specgen/backend/code_spans.hpp>
#include <beman/specgen/backend/element_label.hpp>
#include <beman/specgen/backend/render_fold.hpp>
#include <beman/specgen/foundation/enumeration.hpp>
#include <beman/specgen/foundation/overloaded.hpp>
#include <beman/specgen/ir.hpp>

#include <beman/tree_algorithms/recursion_schemes.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace beman::specgen::backend::mpark {

namespace {

using beman::specgen::foundation::overloaded;

// --- span substitution: the shared substrate's escape hook ----------------

// mpark/wg21 turns on embedded Markdown for `cpp`, `default` and `diff` code
// -- both fenced blocks and inline `code` -- with `@` as the Markdown
// delimiter and `$` as an italics shorthand (`$x$` means `@*x*@`).
//
// The draft's original five span macros are italic, which is why the shorthand
// covers four of them outright: \exposid{x} is \tcode{\placeholder{x}},
// \placeholder{x} is \textit{x}, and \seebelow is \UNSP{see below} --
// \textit{\texttt{...}}. Only a cross-reference needs the general form, since
// its payload is a span with attributes rather than a run of text.
std::string escape_span(const ir::Span& span, std::string_view spelling) {
    return foundation::match(span.kind,
                             foundation::when<ir::SpanKind::ExposId>([&] { return '$' + span.payload + '$'; }),
                             foundation::when<ir::SpanKind::SeeBelow>([&] { return "$see below$"; }),
                             foundation::when<ir::SpanKind::ImplDefined>([&] { return "$implementation-defined$"; }),
                             foundation::when<ir::SpanKind::Placeholder>([&] { return '$' + span.payload + '$'; }),
                             // No parentheses: a Ref span is the *bare* cross-reference the
                             // draft's `\ref` sets inside a comment, and the author writes the
                             // punctuation around it (`// \ref{optional.ctor}, constructors`).
                             // `ir::RefInline` is the parenthesized prose form and keeps its
                             // parens, matching `\iref` -- see PieceRenderer below.
                             //
                             // Wrapped in `@...@`, where the LaTeX backend deliberately leaves this
                             // one kind unwrapped: that exception is a property of the *draft's*
                             // listings setup (`texcl=true` puts comments in TeX mode already), and
                             // mpark has no comment exception at all -- its scanner is purely
                             // delimiter-driven and does not know a comment from any other text.
                             foundation::when<ir::SpanKind::Ref>([&] { return "@[" + span.payload + "]{- .sref}@"; }),
                             foundation::when<ir::SpanKind::LibraryIndex>([&] { return std::string(spelling); }));
}

// Code text is the author's own tokens; it is never re-escaped, and in this
// backend that statement is load-bearing rather than incidental.
//
// mpark's scanner (data/filters/wg21.py) walks for `@`/`$` with no backslash
// handling of any kind, so writing `\@` would put a literal backslash in the
// output rather than escaping anything. A *lone* delimiter is harmless -- the
// scanner needs a closing partner on the same line and leaves an unpaired one
// alone -- so the only misparse available is a same-line pair, and the fix for
// that is the framework's own ```cpp {md=none} attribute override, not an
// escape this function could apply. Neither character is a C++ token, so no
// corpus header contains either; `mpark.test.cpp` pins the pass-through, since
// no golden can (the same shape of hole as `write_json_string`'s).
std::string render_code(const ir::CodeText& code) { return backend::render_code_spans(code, escape_span); }

std::string render_codeblock(const ir::CodeText& code) { return "```cpp\n" + render_code(code) + "\n```\n"; }

// --- prose ------------------------------------------------------------------

// Dispatches one Inline (a Paragraph's Piece) to its markdown rendering. Four
// alternatives (decision visitation-rules' >3 rule), so a named visitor
// struct, mirroring latex.cpp's PieceRenderer.
struct PieceRenderer {
    // Plain prose text, verbatim -- the same treatment the LaTeX backend
    // gives it. The author's prose is the author's in both targets: prose
    // that would need escaping here (a bare `_`, a stray `[`) is prose that
    // would already have needed escaping to reach the draft.
    std::string operator()(const ir::TextInline& v) const { return v.text; }

    // Inline code. Unlike LaTeX there is no whole-span shortcut: backticks
    // supply the code font that `\tcode` supplies there, and `$...$` adds
    // only the italics, so `` `$value$` `` is right for a whole-span exposid
    // and `` `x.$val$` `` for a partial one -- one rule, both cases.
    std::string operator()(const ir::CodeInline& v) const { return '`' + render_code(v.code) + '`'; }

    // A cross-reference to another stable name. `\iref{x}` renders "([x])" in
    // the draft, so the parentheses belong to this rendering and not to the
    // authored prose around it. Unnumbered (`{- .sref}`) for the reason the
    // section headings are: a wording cross-reference reads as the bracketed
    // stable name, not as a section number.
    std::string operator()(const ir::RefInline& v) const { return "([" + v.stable_name + "]{- .sref})"; }

    // A library concept name. The draft's \libconcept sets the code font and
    // links; markdown has only the code font to offer, which is what
    // backticks give.
    std::string operator()(const ir::ConceptRef& v) const { return '`' + v.name + '`'; }
};

std::string render_paragraph(const ir::Paragraph& para) {
    return para | std::views::transform([](const ir::Inline& piece) {
               return std::visit(overloaded{PieceRenderer{}}, piece);
           }) |
           std::views::join | std::ranges::to<std::string>();
}

std::string render_table_cell(const ir::Paragraph& paragraph) {
    return render_paragraph(paragraph) |
           std::views::transform([](const char ch) { return ch == '|' ? std::string{"\\|"} : std::string(1, ch); }) |
           std::views::join | std::ranges::to<std::string>();
}

std::string render_table(const ir::Table2D& table) {
    std::string out = std::format(
        "| | {} | {} |\n|---|---|---|\n", render_table_cell(table.column1), render_table_cell(table.column2));
    out += table.rows | std::views::transform([](const ir::Table2DRow& row) {
               return std::format("| **{}** | {} | {} |\n",
                                  render_table_cell(row.header),
                                  render_table_cell(row.cell1),
                                  render_table_cell(row.cell2));
           }) |
           std::views::join | std::ranges::to<std::string>();
    return out + std::format(": [{}]{{#{}}}\n", render_paragraph(table.caption), table.stable_name);
}

// \libtab2's flat two-column table (issue #74): a plain pandoc pipe table,
// column for column, with no row-heading column to spare.
std::string render_flat_table(const ir::Table1D& table) {
    std::string out =
        std::format("| {} | {} |\n|---|---|\n", render_table_cell(table.column1), render_table_cell(table.column2));
    out += table.rows | std::views::transform([](const ir::Table1DRow& row) {
               return std::format("| {} | {} |\n", render_table_cell(row.cell1), render_table_cell(row.cell2));
           }) |
           std::views::join | std::ranges::to<std::string>();
    return out + std::format(": [{}]{{#{}}}\n", render_paragraph(table.caption), table.stable_name);
}

// --- items --------------------------------------------------------------

// One block's rendering and paragraph-boundary policy. Lists and tables
// belong to the preceding numbered paragraph; an extracted body starts one.
struct RenderedBodyBlock {
    std::string text;
    bool        starts_paragraph = false;
    bool        inline_label     = false;
};

struct BodyRenderer {
    // Authored prose retains its paragraph boundary or explicit continuation.
    RenderedBodyBlock operator()(const ir::BodyParagraph& p) const;
    // Itemizations stay in the paragraph they enumerate.
    RenderedBodyBlock operator()(const ir::Itemize& list) const;
    // Every authored table remains in its source position.
    RenderedBodyBlock operator()(const ir::Table2D& t) const;
    RenderedBodyBlock operator()(const ir::Table1D& t) const;
    // Extraction retains its semantic introduction and its own paragraph.
    RenderedBodyBlock operator()(const ir::EquivalentTo& e) const;
};

RenderedBodyBlock BodyRenderer::operator()(const ir::BodyParagraph& p) const {
    return {render_paragraph(p.text) + '\n', !p.continuation, true};
}

RenderedBodyBlock BodyRenderer::operator()(const ir::Itemize& list) const {
    const std::string items =
        list.items |
        std::views::transform([](const ir::Paragraph& p) { return "- [#.#]{.pnum} " + render_paragraph(p) + '\n'; }) |
        std::views::join | std::ranges::to<std::string>();
    return {items, false, false};
}

RenderedBodyBlock BodyRenderer::operator()(const ir::Table2D& t) const { return {render_table(t), false, false}; }

RenderedBodyBlock BodyRenderer::operator()(const ir::Table1D& t) const { return {render_flat_table(t), false, false}; }

RenderedBodyBlock BodyRenderer::operator()(const ir::EquivalentTo& e) const {
    return {"Equivalent to:\n\n" + render_codeblock(e.code), true, true};
}

std::vector<std::string> element_blocks(const ir::DescriptionElement& element) {
    const std::string label  = std::format("*{}*", backend::element_label(element.kind));
    auto              blocks = std::ranges::fold_left(
        element.body, std::vector<std::string>{}, [&](std::vector<std::string> out, const ir::BodyBlock& block) {
            auto       rendered = std::visit(overloaded{BodyRenderer{}}, block);
            const bool first    = out.empty();
            if (first || rendered.starts_paragraph) {
                const std::string prefix = first ? label + (rendered.inline_label ? ": " : ":\n\n") : std::string{};
                out.push_back("[#]{.pnum} " + prefix + rendered.text);
            } else {
                out.back() += '\n' + rendered.text;
            }
            return out;
        });
    return blocks;
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

std::vector<std::string> element_group_blocks(std::ranges::range auto&& group) {
    return element_blocks(merge_element_group(group));
}

std::string render_item(const ir::SpecItem& item) {
    // Index entries are dropped (design §8: "draft backend expands, others
    // drop"). `\indexlibrarymember` builds the draft's own index; a paper
    // fragment has no index to build.
    // An item with no signatures is a description with no itemdecl -- a
    // class's own wording (design §6), which the draft writes as bare
    // paragraphs in a general subclause. Emitting the fence for it would
    // produce the empty code block design §9 rejects on a synopsis.
    std::string out;
    if (!item.decl.signatures.empty()) {
        out = "```cpp\n";
        out += item.decl.signatures |
               std::views::transform([](const ir::CodeText& sig) { return render_code(sig) + '\n'; }) |
               std::views::join | std::ranges::to<std::string>();
        out += "```\n";
    }

    if (item.descr.elements.empty())
        return out;

    const std::vector<std::string> blocks =
        item.descr.elements |
        std::views::chunk_by(
            [](const ir::DescriptionElement& a, const ir::DescriptionElement& b) { return a.kind == b.kind; }) |
        std::views::transform([](auto&& group) { return element_group_blocks(group); }) | std::views::join |
        std::ranges::to<std::vector>();

    if (!out.empty())
        out += '\n';
    out += blocks | std::views::join_with('\n') | std::ranges::to<std::string>();
    return out;
}

// --- the direct algebra over backend::backend::RenderF ---------------------

// Inherited attribute carrier, handed down through `project` below. Only the
// heading level travels: unlike the LaTeX backend's RenderCtx there is no
// `pnum_base` companion field, because `[#]{.pnum}` means this backend never
// needs to know what number a paragraph will get.
struct RenderCtx {
    int level;
};

// The fold's seed/handle type: a node paired with the RenderCtx its parent
// assigned it, so a Section's own heading level is resolved before the node is
// visited rather than threaded through the algebra's result type.
struct Seeded {
    const ir::Node* node;
    RenderCtx       ctx;
};

// `Seeded`'s std::visit dispatch (decision visitation-rules: named struct,
// ir::Node has four alternatives), member state rather than a capture.
struct SeededProjector {
    RenderCtx ctx;

    // A subsection. The heading depends on *this* node's level, which is
    // known only here during descent, so it is rendered now into
    // `RenderedSectionF::header` -- the field named for exactly this.
    //
    // The form is a deliberate checkpoint call: the IR's own
    // title, then the stable name as an *unnumbered* sref, then `{-}` to keep
    // the paper's section numbering off a heading that carries a stable name
    // of its own. Unnumbered because a facility being proposed is not in the
    // srefs database the numbered form looks up: `{.sref}` alone would warn at
    // paper-build time and discard the title specgen already knows.
    backend::RenderF<Seeded> operator()(const ir::Section& s) const {
        // Markdown has six heading levels; a document nested past that is
        // already past what a paper's outline can express, so the level
        // saturates rather than emitting a run of hashes pandoc would read as
        // paragraph text.
        const int         level = std::min(ctx.level, 6);
        const std::string hashes(static_cast<std::size_t>(level), '#');
        // A hand-written Section may carry no title; emitting the empty one
        // would leave a double space before the stable name.
        std::string header = s.title.empty()
                                 ? std::format("{} [{}]{{- .sref}} {{-}}\n", hashes, s.stable_name)
                                 : std::format("{} {} [{}]{{- .sref}} {{-}}\n", hashes, s.title, s.stable_name);

        const RenderCtx     child_ctx{ctx.level + 1};
        std::vector<Seeded> children =
            s.children |
            std::views::transform([&child_ctx](const ir::Node& child) { return Seeded{&child, child_ctx}; }) |
            std::ranges::to<std::vector>();
        return backend::RenderedSectionF<Seeded>{std::move(header), std::move(children)};
    }

    // The three leaves render independently of heading level, so they pass
    // through unchanged.
    backend::RenderF<Seeded> operator()(const ir::Synopsis& v) const { return v; }
    backend::RenderF<Seeded> operator()(const ir::SpecItem& v) const { return v; }
    backend::RenderF<Seeded> operator()(const ir::FreeParagraph& v) const { return v; }
};

backend::RenderF<Seeded> project(const Seeded& seeded) {
    return std::visit(overloaded{SeededProjector{seeded.ctx}}, *seeded.node);
}

// Dispatches one already-rendered RenderF layer to its markdown. Four
// alternatives (decision visitation-rules' >3 rule), so a named visitor
// struct. The heading has
// already been resolved by `project`, so every case is a plain
// RenderF<std::string> -> std::string mapping.
struct MparkAlgebra {
    std::string operator()(const backend::RenderedSectionF<std::string>& s) const {
        if (s.children.empty())
            return s.header;
        return s.header + "\n" + (s.children | std::views::join_with('\n') | std::ranges::to<std::string>());
    }

    std::string operator()(const ir::Synopsis& v) const { return render_codeblock(v.code); }
    std::string operator()(const ir::SpecItem& v) const { return render_item(v); }
    std::string operator()(const ir::FreeParagraph& v) const {
        return "[#]{.pnum} " + render_paragraph(v.text) + '\n';
    }
};

std::string render_layer(const backend::RenderF<std::string>& layer) {
    return std::visit(overloaded{MparkAlgebra{}}, layer);
}

std::string render_node_to_string(const ir::Node& node, const RenderCtx& ctx) {
    return beman::tree_algorithms::fold_with<std::string>(
        render_layer, backend::render_fmap, project, Seeded{&node, ctx});
}

// `[#]{.pnum}` is only auto-numbered inside a `::: wording` div, and the
// numbering restarts at each such div -- so where the divs go *is* where
// paragraph numbering restarts. One per top-level node puts a restart at each
// `\rSec` root, which is where the draft puts one.
//
// This is done here rather than in the algebra because it is the one decision
// that needs to see the node *list*: inside the fold a Section cannot tell
// whether it is a root or a subsection. The residual is that a nested
// subsection continues its parent's numbering instead of restarting; the
// fragment split (`render --split`, fragments.hpp) cuts at top-level
// sections, turning one div per top-level node into one div per fragment,
// which settles it.
std::string wrap_wording(std::string text) { return "::: wording\n\n" + text + "\n:::\n"; }

// --- paper mode -------------------------------------------------------------

// Under paper mode every paragraph in the fragment is an *added* paragraph, and
// mpark's form for one is the placeholder `x` rather than the auto-numbering
// `#`: the filter (data/filters/wg21.py, process_pnum) treats a part that is
// neither a decimal nor `#` as a literal and leaves the surrounding counter
// alone, which is the point -- an inserted paragraph must not renumber the
// wording it is inserted into.
//
// The literal has to *differ* per paragraph, which is the one thing the two
// choices above do not compose into on their own. A
// literal is literal: emit `[x]{.pnum}` five times and the filter renders five
// paragraphs all numbered "x". So the sequence is `x`, `x+1`, `x+2`, ... --
// exactly what P4307R0 writes by hand ([lex.name], three added paragraphs) --
// and a sublist item takes its lead-in paragraph's literal with `.#` after it,
// giving `x+1.1`, `x+1.2`, since the filter *does* auto-number a `#` part under
// a literal parent.
//
// This runs over already-rendered text rather than inside the algebra, for the
// same reason `wrap_wording` does: it is a property of the document, not of any
// node. Of the *document*, and that is the correction: it used to run per
// wording div, on the reasoning that a per-div reset kept it in step with the
// `#` numbering it replaces. It does not. The filter counts `#` across divs and
// does not count literals at all, so a per-div run emitted `x` again at the top
// of every one -- and a subclause built from more than one div, which is the
// ordinary shape once documented entities interleave with undocumented synopsis
// code, showed every paragraph as "x" (issue #57). Nothing can then refer to
// one of them. Within a div the run was right, which is what made the reset
// easy to miss.
std::string renumber_added(const std::string& text) {
    constexpr std::string_view kPara = "[#]{.pnum}";
    constexpr std::string_view kItem = "[#.#]{.pnum}";

    std::string out;
    out.reserve(text.size());
    unsigned    para = 0;
    std::size_t pos  = 0;
    // substrate generic algorithm: a position-tracking left-to-right walk
    // performing array substitution, the same primitive
    // backend/code_spans.hpp's render_code_spans is marked for -- and here the
    // replacement depends on how many paragraphs the walk has already passed,
    // which is the walk's own position restated, not a separate accumulator.
    while (pos < text.size()) {
        const std::size_t at_para = text.find(kPara, pos);
        const std::size_t at_item = text.find(kItem, pos);
        const std::size_t at      = std::min(at_para, at_item);
        if (at == std::string::npos)
            break;
        out += text.substr(pos, at - pos);
        // A sublist item belongs to the paragraph that introduced it, which is
        // the one just counted -- `element_blocks` always emits a lead-in
        // `[#]{.pnum}` before any `[#.#]`, so `para` is never 0 here.
        const bool  is_item = at == at_item;
        const auto  n       = is_item ? para - 1 : para;
        std::string literal = n == 0 ? std::string{"x"} : std::format("x+{}", n);
        out += std::format("[{}{}]{{.pnum}}", literal, is_item ? ".#" : "");
        pos = at + (is_item ? kItem.size() : kPara.size());
        if (!is_item)
            ++para;
    }
    out += text.substr(pos);
    return out;
}

// Whether a stable name is one of `--new-root`'s own clauses (issues #89,
// #94): equal to a root, or anywhere in its subtree. `demo.detail` and
// `demo.iter.deep.deeper` are both under `demo`; `demolition` is not -- the
// trailing `.` in the second branch is what keeps a prefix match from also
// matching an unrelated longer name that merely starts with the same
// characters. Any root matching is enough, so a paper proposing two headers
// passes `--new-root` twice and both subtrees are its own.
bool under_new_root(std::string_view name, std::span<const std::string> new_roots) {
    return std::ranges::any_of(new_roots, [name](std::string_view root) {
        return name == root || (name.starts_with(root) && name.size() > root.size() && name[root.size()] == '.');
    });
}

// Strips the `.sref` class from every `[name]{- .sref}` this fragment wrote
// for a name under one of `new_roots` (issue #89): a paper's own proposed
// clause is not yet in the srefs database `.sref` looks up, so keeping the
// class there buys nothing but a build-time warning and a dead link to
// eel.is/c++draft/<name>. `new_roots` empty (the default) leaves every
// occurrence untouched.
//
// Runs over already-rendered text rather than inside the algebra, for the
// same reason `renumber_added` above does: this is a property of the
// document (which names are the paper's own), not of any one node, and the
// three call sites that write `{- .sref}` -- the `Ref` span inside a `@…@`
// comment escape, `ir::RefInline`'s parenthesized form, and the Section
// heading -- all write the exact same `{- .sref}` immediately after the
// closing `]`, so one pass here covers all three without threading
// `new_roots` through the render algebra at all.
std::string drop_sref_for_new_root(const std::string& text, std::span<const std::string> new_roots) {
    if (new_roots.empty())
        return text;
    constexpr std::string_view kSref = "]{- .sref}";
    std::string                out;
    out.reserve(text.size());
    std::size_t pos = 0;
    // substrate generic algorithm: the same position-tracking left-to-right
    // walk renumber_added performs, substituting a bracketed name's own
    // trailing class rather than a paragraph-number token.
    while (pos < text.size()) {
        const std::size_t close = text.find(kSref, pos);
        if (close == std::string::npos)
            break;
        const std::size_t open = text.rfind('[', close);
        // No matching `[` in the unconsumed text: not one of this backend's
        // own `.sref` emissions after all, so this occurrence is left alone
        // rather than guessed at.
        if (open == std::string::npos || open < pos) {
            out += text.substr(pos, close + kSref.size() - pos);
            pos = close + kSref.size();
            continue;
        }
        const std::string_view name = std::string_view(text).substr(open + 1, close - open - 1);
        out += text.substr(pos, open - pos);
        out += '[';
        out += name;
        out += ']';
        if (!under_new_root(name, new_roots))
            out += "{- .sref}";
        pos = close + kSref.size();
    }
    out += text.substr(pos);
    return out;
}

} // namespace

// The editing-instruction div, wrapping the whole fragment. One per
// fragment rather than one per entity: which *parts* of a fragment are new is
// a property of the edit being proposed, and the IR holds no record of it --
// design §7 puts add/rm markup behind a diff of two header revisions and says
// it is "out of scope now, by hand at first". So paper mode says "all of this
// is added", which is true of a fragment specifying a new facility, and the
// including paper still writes the instruction naming where it goes.
std::string wrap_added(std::string text) { return "::: add\n\n" + text + "\n:::\n"; }

std::string render_to_string(const ir::Document& doc, const Options& options) {
    const RenderCtx                ctx{options.base_heading_level};
    const bool                     paper    = options.paper_mode;
    const std::vector<std::string> rendered = doc.nodes | std::views::transform([&](const ir::Node& node) {
                                                  std::string text = render_node_to_string(node, ctx);
                                                  // A synopsis holds no numbered paragraph, so wrapping one would
                                                  // put an empty wording div around a code fence. Every other node
                                                  // kind either is a numbered paragraph or contains them.
                                                  if (std::holds_alternative<ir::Synopsis>(node))
                                                      return text;
                                                  return wrap_wording(std::move(text));
                                              }) |
                                              std::ranges::to<std::vector>();
    std::string                    out      = rendered | std::views::join_with('\n') | std::ranges::to<std::string>();
    out                                     = drop_sref_for_new_root(out, options.new_roots);
    // Renumbered once, over the joined document, so the added-paragraph run
    // ascends across every wording div in it rather than restarting inside
    // each (issue #57). Rendering a fragment renders a document, so this is
    // the one ascending run per subclause the placeholders are for.
    return paper ? wrap_added(renumber_added(out)) : out;
}

std::string render_to_string(const ir::SpecItem& item, const Options& options) {
    std::string text = drop_sref_for_new_root(render_item(item), options.new_roots);
    if (!options.paper_mode)
        return wrap_wording(std::move(text));
    return wrap_added(wrap_wording(renumber_added(text)));
}

} // namespace beman::specgen::backend::mpark
