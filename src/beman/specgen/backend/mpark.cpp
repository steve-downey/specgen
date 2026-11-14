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
// A span inside a changed run of code (the wording diff's): the run has
// opened the `@...@` already, so the shorthand `$x$` -- which stands for
// `@*x*@` -- gives way to the `*x*` it abbreviates, and a cross-reference to
// its bare bracketed form.
std::string escape_span_in_run(const ir::Span& span, std::string_view spelling) {
    return foundation::match(span.kind,
                             foundation::when<ir::SpanKind::ExposId>([&] { return '*' + span.payload + '*'; }),
                             foundation::when<ir::SpanKind::SeeBelow>([&] { return "*see below*"; }),
                             foundation::when<ir::SpanKind::ImplDefined>([&] { return "*implementation-defined*"; }),
                             foundation::when<ir::SpanKind::Placeholder>([&] { return '*' + span.payload + '*'; }),
                             foundation::when<ir::SpanKind::Ref>([&] { return "[" + span.payload + "]{- .sref}"; }),
                             foundation::when<ir::SpanKind::LibraryIndex>([&] { return std::string(spelling); }));
}

// A changed run of code in the framework's embedded-Markdown form:
// `@[...]{.add}@` / `@[...]{.rm}@`, the manual's own spelling for an inline
// change inside a code block.
std::string wrap_code_run(ir::Change change, std::string_view text) {
    return foundation::match(
        change,
        foundation::when<ir::Change::Same>([&] { return std::string(text); }),
        foundation::when<ir::Change::Added>([&] { return "@[" + std::string(text) + "]{.add}@"; }),
        foundation::when<ir::Change::Removed>([&] { return "@[" + std::string(text) + "]{.rm}@"; }),
        foundation::when<ir::Change::Elided>([&] { return std::string(text); }));
}

std::string render_code(const ir::CodeText& code) {
    return backend::render_code_spans(code, escape_span, escape_span_in_run, wrap_code_run);
}

// A marked node or element in the framework's editing-instruction div,
// `::: add` / `::: rm`; an unmarked one as it is. One div closes around a
// whole marked section, item or element: the mark is on the node, so the div
// is too.
std::string wrap_div(ir::Change change, std::string text) {
    return foundation::match(change,
                             foundation::when<ir::Change::Same>([&] { return text; }),
                             foundation::when<ir::Change::Added>([&] { return "::: add\n\n" + text + "\n:::\n"; }),
                             foundation::when<ir::Change::Removed>([&] { return "::: rm\n\n" + text + "\n:::\n"; }),
                             foundation::when<ir::Change::Elided>([&] { return text; }));
}

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

    // A placeholder name and a C++17 named requirement. The draft sets both
    // in italics, and markdown has italics, so that is what they get: the
    // look of the LaTeX rendering, not a semantic class the framework lacks.
    std::string operator()(const ir::PlaceholderInline& v) const { return '*' + v.text + '*'; }
    std::string operator()(const ir::OldConceptRef& v) const { return "*Cpp17" + v.name + '*'; }

    // A changed run of prose (the wording diff's mark): the framework's
    // `[...]{.add}` / `[...]{.rm}` spans around the run rendered as it would
    // be unmarked. Defined after `render_run` below, which dispatches the
    // run's `PlainInline`s -- a different, non-recursive variant -- through
    // this same visitor's other four cases.
    std::string operator()(const ir::ChangedInline& run) const;
};

std::string render_run(const std::vector<ir::PlainInline>& inlines) {
    return inlines | std::views::transform([](const ir::PlainInline& piece) {
               return std::visit(overloaded{PieceRenderer{}}, piece);
           }) |
           std::views::join | std::ranges::to<std::string>();
}

std::string PieceRenderer::operator()(const ir::ChangedInline& run) const {
    const std::string inner = render_run(run.inlines);
    return foundation::match(run.change,
                             foundation::when<ir::Change::Same>([&] { return inner; }),
                             foundation::when<ir::Change::Added>([&] { return '[' + inner + "]{.add}"; }),
                             foundation::when<ir::Change::Removed>([&] { return '[' + inner + "]{.rm}"; }),
                             foundation::when<ir::Change::Elided>([&] { return inner; }));
}

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

struct LeafBodyRenderer {
    bool numbered_lists = true;
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
    if (p.change == ir::Change::Elided)
        return {"[…]\n", false, false};
    return {render_paragraph(p.text) + '\n', !p.continuation, true};
}

RenderedBodyBlock LeafBodyRenderer::operator()(const ir::Itemize& list) const {
    const std::string items =
        list.items | std::views::transform([this](const ir::Paragraph& p) {
            return std::format("- {}{}\n", numbered_lists ? "[#.#]{.pnum} " : "", render_paragraph(p));
        }) |
        std::views::join | std::ranges::to<std::string>();
    return {items, false, false};
}

RenderedBodyBlock LeafBodyRenderer::operator()(const ir::Table2D& t) const { return {render_table(t), false, false}; }

RenderedBodyBlock LeafBodyRenderer::operator()(const ir::Table1D& t) const {
    return {render_flat_table(t), false, false};
}

RenderedBodyBlock LeafBodyRenderer::operator()(const ir::EquivalentTo& e) const {
    return {"Equivalent to:\n\n" + render_codeblock(e.code), true, true};
}

RenderedBodyBlock LeafBodyRenderer::operator()(const ir::AuthoredCode& code) const {
    const std::string text = render_code(code.code);
    const auto        runs = std::ranges::fold_left(text, std::pair{0uz, 0uz}, [](auto state, char ch) {
        state.first  = ch == '`' ? state.first + 1 : 0;
        state.second = std::max(state.first, state.second);
        return state;
    });
    const std::string fence(std::max(3uz, runs.second + 1), '`');
    return {std::format("{0}cpp\n{1}\n{0}\n", fence, text), false, false};
}

std::string render_informative_body(const ir::InformativeBody& body);
std::string render_informative_body(const ir::InformativeBody& body) {
    return body | std::views::transform([](const ir::InformativeBlock& block) {
               return wrap_div(ir::block_change(block), std::visit(overloaded{LeafBodyRenderer{false}}, block).text);
           }) |
           std::views::join_with('\n') | std::ranges::to<std::string>();
}

struct BodyRenderer : LeafBodyRenderer {
    using LeafBodyRenderer::operator();
    RenderedBodyBlock operator()(const ir::Note& note) const;
    RenderedBodyBlock operator()(const ir::Example& example) const;
};

RenderedBodyBlock BodyRenderer::operator()(const ir::Note& note) const {
    return {std::format("::: note\n\n{}\n:::\n", render_informative_body(note.body)), false, false};
}

RenderedBodyBlock BodyRenderer::operator()(const ir::Example& example) const {
    return {std::format("::: example\n\n{}\n:::\n", render_informative_body(example.body)), false, false};
}

std::vector<std::string> body_blocks(const ir::DescriptionBody& body, std::optional<ir::ElementKind> kind);

std::vector<std::string> body_blocks(const ir::DescriptionBody& body, std::optional<ir::ElementKind> kind) {
    const std::string label  = kind ? std::format("*{}*", backend::element_label(*kind)) : std::string{};
    auto              blocks = std::ranges::fold_left(
        body, std::vector<std::string>{}, [&](std::vector<std::string> out, const ir::BodyBlock& block) {
            auto       rendered = std::visit(overloaded{BodyRenderer{}}, block);
            const bool first    = out.empty();
            // A marked block (the wording diff's) closes in its div alone:
            // around its whole numbered paragraph when it opens one, so the
            // number is inside the mark and an added paragraph takes the
            // literal, and around just itself when it continues the paragraph
            // before it.
            const ir::Change change = ir::block_change(block);
            if (change == ir::Change::Elided) {
                out.push_back(rendered.text);
            } else if (first || rendered.starts_paragraph) {
                const std::string prefix =
                    first && kind ? label + (rendered.inline_label ? ": " : ":\n\n") : std::string{};
                out.push_back(wrap_div(change, "[#]{.pnum} " + prefix + rendered.text));
            } else {
                out.back() += '\n' + wrap_div(change, rendered.text);
            }
            return out;
        });
    return blocks;
}

std::vector<std::string> element_blocks(const ir::DescriptionElement& element);
std::vector<std::string> element_blocks(const ir::DescriptionElement& element) {
    return body_blocks(element.body, element.kind);
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

    if (!beman::specgen::ir::has_description(item.descr))
        return wrap_div(item.change, std::move(out));

    // Grouped by mark as well as kind, each group in its own div when marked
    // -- the same rule, for the same reason, as latex.cpp's render_item.
    std::vector<std::string> groups = body_blocks(item.descr.leading, std::nullopt);
    groups.append_range(item.descr.elements |
                        std::views::chunk_by([](const ir::DescriptionElement& a, const ir::DescriptionElement& b) {
                            return a.kind == b.kind && a.change == b.change;
                        }) |
                        std::views::transform([](auto&& group) {
                            return wrap_div(std::ranges::begin(group)->change,
                                            element_group_blocks(group) | std::views::join_with('\n') |
                                                std::ranges::to<std::string>());
                        }));

    if (!out.empty())
        out += '\n';
    out += groups | std::views::join_with('\n') | std::ranges::to<std::string>();
    return wrap_div(item.change, std::move(out));
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
        std::string header = s.title.empty() ? std::format("{} [{}]{{- .sref}} {{-}}\n", hashes, s.stable_name)
                                             : std::format("{} {} [{}]{{- .sref}} {{-}}\n",
                                                           hashes,
                                                           backend::render_section_title(s.title, '`'),
                                                           s.stable_name);

        const RenderCtx     child_ctx{ctx.level + 1};
        std::vector<Seeded> children =
            s.children |
            std::views::transform([&child_ctx](const ir::Node& child) { return Seeded{&child, child_ctx}; }) |
            std::ranges::to<std::vector>();
        return backend::RenderedSectionF<Seeded>{std::move(header), std::move(children), s.change};
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
    // A marked section (the wording diff's) closes in its div here, where
    // header and children are first seen together.
    std::string operator()(const backend::RenderedSectionF<std::string>& s) const {
        if (s.children.empty())
            return wrap_div(s.change, s.header);
        return wrap_div(s.change,
                        s.header + "\n" + (s.children | std::views::join_with('\n') | std::ranges::to<std::string>()));
    }

    std::string operator()(const ir::Synopsis& v) const { return wrap_div(v.change, render_codeblock(v.code)); }
    std::string operator()(const ir::SpecItem& v) const { return render_item(v); }
    // An elided free paragraph (the wording diff's `items` context) is the
    // ellipsis a paper writes for wording left out, with no paragraph number.
    std::string operator()(const ir::FreeParagraph& v) const {
        if (v.change == ir::Change::Elided)
            return "[…]\n";
        return wrap_div(v.change, "[#]{.pnum} " + render_paragraph(v.text) + '\n');
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
// The renumbering walk's state: how many added paragraphs have been
// numbered, and whether the paragraph most recently passed -- added or kept
// -- was an added one, which decides what an added sublist item under it is
// numbered from.
struct Renumbering {
    unsigned para       = 0;
    bool     last_added = false;
};

// One line's paragraph-number tokens, numbered on from `state`. Inside an
// added region a paragraph takes the literal `x`, `x+1`, ... and a sublist
// item takes its lead-in paragraph's literal with `.#` after it -- when that
// lead-in was itself added. An added item under a *kept* paragraph takes
// `[#.x]{.pnum}` instead: the paragraph keeps the number the draft gives it,
// and the new item is the literal beneath it, which is how mpark's filter
// reads a literal part (it leaves the counter beside it alone).
std::string renumber_line(std::string_view line, Renumbering& state) {
    constexpr std::string_view kPara = "[#]{.pnum}";
    constexpr std::string_view kItem = "[#.#]{.pnum}";

    std::string out;
    out.reserve(line.size());
    std::size_t pos = 0;
    // substrate generic algorithm: a position-tracking left-to-right walk
    // performing array substitution, the same primitive
    // backend/code_spans.hpp's render_code_spans is marked for -- and here the
    // replacement depends on how many paragraphs the walk has already passed,
    // which is the walk's own position restated, not a separate accumulator.
    while (pos < line.size()) {
        const std::size_t at_para = line.find(kPara, pos);
        const std::size_t at_item = line.find(kItem, pos);
        const std::size_t at      = std::min(at_para, at_item);
        if (at == std::string::npos)
            break;
        out += line.substr(pos, at - pos);
        const bool is_item = at == at_item;
        if (!is_item) {
            out += std::format("[{}]{{.pnum}}", state.para == 0 ? std::string{"x"} : std::format("x+{}", state.para));
            ++state.para;
            state.last_added = true;
        } else if (state.last_added) {
            const auto n = state.para - 1;
            out += std::format("[{}.#]{{.pnum}}", n == 0 ? std::string{"x"} : std::format("x+{}", n));
        } else {
            out += "[#.x]{.pnum}";
        }
        pos = at + (is_item ? kItem.size() : kPara.size());
    }
    out += line.substr(pos);
    return out;
}

// A line outside every added region passes through, but a paragraph token on
// it is still a paragraph passed: the next added sublist item is under it.
std::string pass_line(std::string_view line, Renumbering& state) {
    if (line.contains("[#]{.pnum}"))
        state.last_added = false;
    return std::string(line);
}

// Under the wording diff only an *added* paragraph takes the literal: a
// retained one keeps `[#]{.pnum}` and its number in the draft, and a removed
// one keeps it too, since the instruction removes a numbered paragraph. Which
// paragraphs are added is a property of the rendered text, read back off it:
// the editing-instruction divs this backend wrote. A line walk keeps the
// stack of open fenced divs -- `::: add`, `::: rm`, `::: wording` are the
// only divs this backend writes -- and renumbers while an `add` is open. One
// ascending run over the whole document, as before (issue #57).
std::string renumber_added(const std::string& text) {
    std::vector<std::string_view> open;
    Renumbering                   state;
    std::string                   out;
    out.reserve(text.size());
    const bool             trailing_newline = text.ends_with('\n');
    const std::string_view body = std::string_view(text).substr(0, text.size() - (trailing_newline ? 1 : 0));
    // substrate generic algorithm: a line walk whose state is the stack of
    // open divs and the paragraph count together -- a fold with two
    // accumulators, written as the loop it is.
    for (auto&& piece : body | std::views::split('\n')) {
        const std::string_view line(piece.begin(), piece.end());
        if (line == "::: add" || line == "::: rm" || line == "::: wording")
            open.push_back(line);
        else if (line == ":::" && !open.empty())
            open.pop_back();
        const bool added = std::ranges::find(open, std::string_view{"::: add"}) != open.end();
        out += added ? renumber_line(line, state) : pass_line(line, state);
        out += '\n';
    }
    if (!trailing_newline && !out.empty())
        out.pop_back();
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

// A top-level node rendered unmarked, beside the mark it carried: the
// document render closes the div itself, after the wording div, so that a run
// of adjacent top-level nodes with one mark shares one editing-instruction
// div -- a fragment that is wholly new is one `::: add`, outside its wording
// divs, which is where paper mode has always put it.
struct RenderedTop {
    ir::Change  change;
    std::string text;
};

std::string render_to_string(const ir::Document& doc, const Options& options) {
    const RenderCtx ctx{options.base_heading_level};
    // Paper mode is the wording diff with an empty baseline
    // (docs/plans/wording-diff.md): every top-level node marked added, and
    // the marks then render exactly as a diff's would.
    const std::optional<std::vector<ir::Node>> marked =
        options.paper_mode ? std::optional{ir::all_marked(doc.nodes, ir::Change::Added)} : std::nullopt;
    const std::vector<ir::Node>&   nodes = marked ? *marked : doc.nodes;
    const std::vector<RenderedTop> rendered =
        nodes | std::views::transform([&ctx](const ir::Node& node) {
            std::string text = render_node_to_string(
                ir::with_change(node,
                                ir::change_of(node) == ir::Change::Elided ? ir::Change::Elided : ir::Change::Same),
                ctx);
            // A synopsis holds no numbered paragraph, so wrapping one would
            // put an empty wording div around a code fence. Every other node
            // kind either is a numbered paragraph or contains them.
            if (!std::holds_alternative<ir::Synopsis>(node))
                text = wrap_wording(std::move(text));
            return RenderedTop{ir::change_of(node), std::move(text)};
        }) |
        std::ranges::to<std::vector>();
    std::string out =
        rendered |
        std::views::chunk_by([](const RenderedTop& a, const RenderedTop& b) { return a.change == b.change; }) |
        std::views::transform([](auto&& chunk) {
            return wrap_div(std::ranges::begin(chunk)->change,
                            chunk | std::views::transform(&RenderedTop::text) | std::views::join_with('\n') |
                                std::ranges::to<std::string>());
        }) |
        std::views::join_with('\n') | std::ranges::to<std::string>();
    out = drop_sref_for_new_root(out, options.new_roots);
    // Renumbered once, over the joined document, so the added-paragraph run
    // ascends across every wording div in it rather than restarting inside
    // each (issue #57). Rendering a fragment renders a document, so this is
    // the one ascending run per subclause the placeholders are for.
    return renumber_added(out);
}

std::string render_to_string(const ir::SpecItem& item, const Options& options) {
    const ir::Change change = options.paper_mode ? ir::Change::Added : item.change;
    ir::SpecItem     copy   = item;
    copy.change             = ir::Change::Same;
    const std::string text  = drop_sref_for_new_root(render_item(copy), options.new_roots);
    return renumber_added(wrap_div(change, wrap_wording(text)));
}

} // namespace beman::specgen::backend::mpark
