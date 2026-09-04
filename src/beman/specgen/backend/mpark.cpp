// src/beman/specgen/backend/mpark.cpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The mpark/wg21 backend, written as a direct algebra over
// backend::common::RenderF -- the same shape backend/latex.cpp uses, and
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

#include <beman/specgen/backend/common.hpp>
#include <beman/specgen/foundation/overloaded.hpp>

#include <beman/tree_algorithms/recursion_schemes.hpp>

#include <algorithm>
#include <format>
#include <ranges>
#include <string>
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
    switch (span.kind) {
    case ir::SpanKind::ExposId:
        return '$' + span.payload + '$';
    case ir::SpanKind::SeeBelow:
        return "$see below$";
    case ir::SpanKind::ImplDefined:
        return "$implementation-defined$";
    case ir::SpanKind::Placeholder:
        return '$' + span.payload + '$';
    case ir::SpanKind::Ref:
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
        return "@[" + span.payload + "]{- .sref}@";
    case ir::SpanKind::LibraryIndex:
        return std::string(spelling);
    }
    std::unreachable(); // SpanKind is exhaustively handled above
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
std::string render_code(const ir::CodeText& code) { return common::render_code_spans(code, escape_span); }

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

// --- items --------------------------------------------------------------

// One DescriptionElement's numbered paragraphs, in emission order -- the
// counterpart of latex.cpp's element_blocks, block for block. Each entry is
// one already-terminated `[#]{.pnum}` unit; the caller joins these, and every
// other element's, with a blank-line separator.
//
// The element's label ("*Effects*: ") comes from common::element_label and
// appears exactly once, on whichever block is emitted first -- the same rule
// the LaTeX backend applies to its `\effects` macro.
std::vector<std::string> element_blocks(const ir::DescriptionElement&   element,
                                        const std::vector<ir::Table2D>& tables) {
    std::vector<std::string> blocks;
    const std::string        label = std::format("*{}*: ", common::element_label(element.kind));

    if (!element.paragraphs.empty()) {
        blocks.push_back("[#]{.pnum} " + label + render_paragraph(element.paragraphs.front()) + '\n');
        blocks.append_range(element.paragraphs | std::views::drop(1) |
                            std::views::transform([](const ir::Paragraph& para) {
                                return "[#]{.pnum} " + render_paragraph(para) + '\n';
                            }));
    }

    if (element.itemize) {
        // A bulleted sublist of a numbered paragraph is numbered (6.1), (6.2)
        // in the draft, which is exactly what `[#.#]{.pnum}` produces inside a
        // `::: wording` div. The blank line before the list is required:
        // without it pandoc reads the first bullet as a lazy continuation of
        // the lead-in paragraph.
        const std::string items = element.itemize->items | std::views::transform([](const ir::Paragraph& item) {
                                      return "- [#.#]{.pnum} " + render_paragraph(item) + '\n';
                                  }) |
                                  std::views::join | std::ranges::to<std::string>();

        // Same placement rule as the LaTeX backend: the list belongs to the
        // paragraph it enumerates, so it follows the lead-in prose when there
        // is one and opens its own numbered paragraph -- carrying the label,
        // since it is then the element's only content -- when there is not.
        if (blocks.empty())
            blocks.push_back("[#]{.pnum} " + std::format("*{}*:", common::element_label(element.kind)) + "\n\n" +
                             items);
        else
            blocks.back() += '\n' + items;
    }

    const std::string rendered_tables =
        tables | std::views::transform(render_table) | std::views::join_with('\n') | std::ranges::to<std::string>();
    if (!rendered_tables.empty()) {
        if (blocks.empty())
            blocks.push_back("[#]{.pnum} " + std::format("*{}*:\n\n", common::element_label(element.kind)) +
                             rendered_tables);
        else
            blocks.back() += '\n' + rendered_tables;
    }

    if (element.equivalent) {
        const bool needs_label = blocks.empty();
        blocks.push_back("[#]{.pnum} " + (needs_label ? label : std::string{}) + "Equivalent to:\n\n" +
                         render_codeblock(element.equivalent->code));
    }

    return blocks;
}

// (design §5.2): an authored `\mandates`/`\constraints` and code's
// derived twin are one description, so a run of same-kind elements folds into
// one before rendering and the label is emitted once. Behaviourally identical
// to latex.cpp's merge_element_group; kept as its own copy rather than hoisted
// into backend/common.hpp because it is three lines of ranges over a type both
// backends already see, and a shared helper here would be the wrong seam --
// what the two backends share is the *substrate* (RenderF, render_code_spans),
// not their item-assembly policy, which is where a third backend is most
// likely to differ.
ir::DescriptionElement merge_element_group(std::ranges::range auto&& group) {
    ir::DescriptionElement out;
    out.kind = std::ranges::begin(group)->kind;

    out.paragraphs = group |
                     std::views::transform([](const ir::DescriptionElement& e) -> const std::vector<ir::Paragraph>& {
                         return e.paragraphs;
                     }) |
                     std::views::join | std::ranges::to<std::vector>();

    std::vector<ir::Paragraph> items =
        group | std::views::filter([](const auto& e) { return e.itemize.has_value(); }) |
        std::views::transform([](const auto& e) -> const std::vector<ir::Paragraph>& { return e.itemize->items; }) |
        std::views::join | std::ranges::to<std::vector>();
    if (!items.empty())
        out.itemize = ir::Itemize{std::move(items)};

    const auto with_equivalent = std::ranges::find_if(group, [](const auto& e) { return e.equivalent.has_value(); });
    if (with_equivalent != std::ranges::end(group))
        out.equivalent = with_equivalent->equivalent;

    return out;
}

std::vector<std::string> element_group_blocks(std::ranges::range auto&& group) {
    const std::vector<ir::Table2D> tables =
        group | std::views::filter([](const auto& e) { return e.table.has_value(); }) |
        std::views::transform([](const auto& e) -> const ir::Table2D& { return *e.table; }) |
        std::ranges::to<std::vector>();
    return element_blocks(merge_element_group(group), tables);
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

// --- the direct algebra over backend::common::RenderF ---------------------

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
    common::RenderF<Seeded> operator()(const ir::Section& s) const {
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
        return common::RenderedSectionF<Seeded>{std::move(header), std::move(children)};
    }

    // The three leaves render independently of heading level, so they pass
    // through unchanged.
    common::RenderF<Seeded> operator()(const ir::Synopsis& v) const { return v; }
    common::RenderF<Seeded> operator()(const ir::SpecItem& v) const { return v; }
    common::RenderF<Seeded> operator()(const ir::FreeParagraph& v) const { return v; }
};

common::RenderF<Seeded> project(const Seeded& seeded) {
    return std::visit(overloaded{SeededProjector{seeded.ctx}}, *seeded.node);
}

// Dispatches one already-rendered RenderF layer to its markdown. Four
// alternatives (decision visitation-rules' >3 rule), so a named visitor
// struct. The heading has
// already been resolved by `project`, so every case is a plain
// RenderF<std::string> -> std::string mapping.
struct MparkAlgebra {
    std::string operator()(const common::RenderedSectionF<std::string>& s) const {
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

std::string render_layer(const common::RenderF<std::string>& layer) {
    return std::visit(overloaded{MparkAlgebra{}}, layer);
}

std::string render_node_to_string(const ir::Node& node, const RenderCtx& ctx) {
    return beman::tree_algorithms::fold_with<std::string>(
        render_layer, common::render_fmap, project, Seeded{&node, ctx});
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
// same reason `wrap_wording` does: it is a property of the enclosing div, not
// of any node. Per-div is also what gives the counter its reset for free, and
// keeps it in step with the `#` numbering it replaces.
std::string renumber_added(const std::string& text) {
    constexpr std::string_view kPara = "[#]{.pnum}";
    constexpr std::string_view kItem = "[#.#]{.pnum}";

    std::string out;
    out.reserve(text.size());
    unsigned    para = 0;
    std::size_t pos  = 0;
    // substrate generic algorithm: a position-tracking left-to-right walk
    // performing array substitution, the same primitive
    // backend/common.hpp's render_code_spans is marked for -- and here the
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
                                                  // put an empty wording div around a code fence -- and, for the
                                                  // same reason, there is nothing in one to renumber. Every other
                                                  // node kind either is a numbered paragraph or contains them.
                                                  if (std::holds_alternative<ir::Synopsis>(node))
                                                      return text;
                                                  return wrap_wording(paper ? renumber_added(text) : std::move(text));
                                              }) |
                                              std::ranges::to<std::vector>();
    std::string                    out      = rendered | std::views::join_with('\n') | std::ranges::to<std::string>();
    return paper ? wrap_added(std::move(out)) : out;
}

std::string render_to_string(const ir::SpecItem& item, const Options& options) {
    std::string text = render_item(item);
    if (!options.paper_mode)
        return wrap_wording(std::move(text));
    return wrap_added(wrap_wording(renumber_added(text)));
}

} // namespace beman::specgen::backend::mpark
