// src/beman/specgen/backend/org.cpp                                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The org-mode backend, written as a direct algebra over
// backend::common::RenderF -- the third file in this directory with that
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
//      produces them (`common::draft_span_codeblock`) rather than each
//      spelling the convention out.
//
//   3. Prose is org, not LaTeX: `/Effects/: ` for a description element,
//      `~code~` for a code inline, `([optional.general])` for a
//      cross-reference. `@...@` is a listings option and is inert out here, so
//      a code inline that carries a *span* -- and only such an inline --
//      leaves org for an `@@latex:...@@` export snippet.
//
//   4. No paragraph numbers, and no `#+begin_wording` wrapper. `\pnum` and
//      `[#]{.pnum}` are self-numbering and org has no equivalent, so numbering
//      here would be the first that specgen itself counted; and design §8's
//      "the including document owns framing" applies with nothing to override
//      it, since org's `wording` environment restyles section numbering rather
//      than enabling anything the fragment needs.

#include <beman/specgen/backend/org.hpp>

#include <beman/specgen/backend/common.hpp>
#include <beman/specgen/foundation/overloaded.hpp>

#include <beman/tree_algorithms/recursion_schemes.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace beman::specgen::backend::org {

namespace {

using beman::specgen::foundation::overloaded;

// --- code -------------------------------------------------------------------

// The span escape is `common::draft_span_codeblock` -- the draft's own, not
// one of this backend's -- for the reason in note 2 at the top of this file.
// It is passed to the same `render_code_spans` extension point every backend
// plugs into (decision backend-direct-algebra).
std::string escape_span(const ir::Span& span, std::string_view spelling) {
    if (span.kind == ir::SpanKind::LibraryIndex)
        return std::string(spelling); // Paper fragments do not build an index.
    return common::draft_span_codeblock(span, spelling);
}

std::string render_code(const ir::CodeText& code) { return common::render_code_spans(code, escape_span); }

// One special block. `env` is the draft environment name org will export to,
// which is also the org block name -- that identity is the whole point of
// using a special block, and is why this takes the name rather than deciding
// it.
std::string render_block(std::string_view env, const ir::CodeText& code) {
    return std::format("#+begin_{}\n{}\n#+end_{}\n", env, render_code(code), env);
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

// Dispatches one Inline (a Paragraph's Piece) to its org rendering. Four
// alternatives (decision visitation-rules' >3 rule), so a named visitor
// struct, mirroring the PieceRenderer in each of the other two backends.
struct PieceRenderer {
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
            return "@@latex:" + common::draft_code_inline(v) + "@@";
        return '~' + v.code.text + '~';
    }

    // A cross-reference to another stable name. `\iref{x}` renders "([x])" in
    // the draft, so the parentheses belong to this rendering and not to the
    // authored prose around it -- and plain text is what `view-maybe.org`
    // already writes ("the exposition-only dereferenceable concept
    // ([iterator.synopsis])"). A `:CUSTOM_ID:` drawer on the heading plus a
    // real org link is deliberately declined: nothing
    // needs resolving yet, and adding the target later moves no heading.
    std::string operator()(const ir::RefInline& v) const { return "([" + v.stable_name + "])"; }

    // A library concept name. The draft's \libconcept sets the code font and
    // links; org has only the code font to offer, which is what `~...~` gives
    // -- the same trade the mpark backend makes with backticks.
    std::string operator()(const ir::ConceptRef& v) const { return '~' + v.name + '~'; }
};

std::string render_paragraph(const ir::Paragraph& para) {
    return para | std::views::transform([](const ir::Inline& piece) {
               return std::visit(overloaded{PieceRenderer{}}, piece);
           }) |
           std::views::join | std::ranges::to<std::string>();
}

std::string render_table_cell(const ir::Paragraph& paragraph) {
    return render_paragraph(paragraph) | std::views::transform([](const char ch) {
               return ch == '|' ? std::string{"\\vert{}"} : std::string(1, ch);
           }) |
           std::views::join | std::ranges::to<std::string>();
}

std::string render_table(const ir::Table2D& table) {
    std::string out = std::format("#+name: {}\n#+caption: {}\n| | {} | {} |\n|-\n",
                                  table.stable_name,
                                  render_paragraph(table.caption),
                                  render_table_cell(table.column1),
                                  render_table_cell(table.column2));
    out += table.rows | std::views::transform([](const ir::Table2DRow& row) {
               return std::format("| {} | {} | {} |\n",
                                  render_table_cell(row.header),
                                  render_table_cell(row.cell1),
                                  render_table_cell(row.cell2));
           }) |
           std::views::join | std::ranges::to<std::string>();
    return out;
}

// --- items --------------------------------------------------------------

// One DescriptionElement's paragraphs, in emission order -- the counterpart of
// the same function in each of the other two backends, block for block. The
// caller joins these, and every other element's, with a blank-line separator.
//
// The element's label ("/Effects/: ") comes from common::element_label and
// appears exactly once, on whichever block is emitted first. Italic rather
// than bold because that is what the draft's `\Fundesc` sets and what
// `view-maybe.org` writes by hand; there is no paragraph number in front of it
// (note 4 at the top of this file).
std::vector<std::string> element_blocks(const ir::DescriptionElement&   element,
                                        const std::vector<ir::Table2D>& tables) {
    std::vector<std::string> blocks;
    const std::string        label = std::format("/{}/: ", common::element_label(element.kind));

    if (!element.paragraphs.empty()) {
        blocks.push_back(label + render_paragraph(element.paragraphs.front()) + '\n');
        blocks.append_range(
            element.paragraphs | std::views::drop(1) |
            std::views::transform([](const ir::Paragraph& para) { return render_paragraph(para) + '\n'; }));
    }

    if (element.itemize) {
        const std::string items =
            element.itemize->items |
            std::views::transform([](const ir::Paragraph& item) { return "- " + render_paragraph(item) + '\n'; }) |
            std::views::join | std::ranges::to<std::string>();

        // Same placement rule as the other two backends: the list belongs to
        // the paragraph it enumerates, so it follows the lead-in prose when
        // there is one and opens the element -- carrying the label, since it is
        // then the element's only content -- when there is not. The blank line
        // before it is not required by org the way it is by pandoc, but it is
        // what makes the fragment read as the draft's own layout does.
        if (blocks.empty())
            blocks.push_back(std::format("/{}/:", common::element_label(element.kind)) + "\n\n" + items);
        else
            blocks.back() += '\n' + items;
    }

    const std::string rendered_tables =
        tables | std::views::transform(render_table) | std::views::join_with('\n') | std::ranges::to<std::string>();
    if (!rendered_tables.empty()) {
        if (blocks.empty())
            blocks.push_back(std::format("/{}/:\n\n", common::element_label(element.kind)) + rendered_tables);
        else
            blocks.back() += '\n' + rendered_tables;
    }

    if (element.equivalent) {
        const bool needs_label = blocks.empty();
        blocks.push_back((needs_label ? label : std::string{}) + "Equivalent to:\n\n" +
                         render_block("codeblock", element.equivalent->code));
    }

    return blocks;
}

// (design §5.2): an authored `\mandates`/`\constraints` and code's
// derived twin are one description, so a run of same-kind elements folds into
// one before rendering and the label is emitted once. The third copy of this
// function, and kept a copy for the reason mpark.cpp records: what the
// backends share is the *substrate*, not their item-assembly policy.
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
    // fragment has none to build. Note this is a *policy* about papers rather
    // than about the target: unlike mpark, this backend could emit the macro
    // and have it work, since the draft's index machinery is loaded.
    std::string out = "#+begin_itemdecl\n";
    out += item.decl.signatures |
           std::views::transform([](const ir::CodeText& sig) { return render_code(sig) + '\n'; }) | std::views::join |
           std::ranges::to<std::string>();
    out += "#+end_itemdecl\n";

    if (item.descr.elements.empty())
        return out;

    const std::vector<std::string> blocks =
        item.descr.elements |
        std::views::chunk_by(
            [](const ir::DescriptionElement& a, const ir::DescriptionElement& b) { return a.kind == b.kind; }) |
        std::views::transform([](auto&& group) { return element_group_blocks(group); }) | std::views::join |
        std::ranges::to<std::vector>();

    out += '\n';
    out += blocks | std::views::join_with('\n') | std::ranges::to<std::string>();
    return out;
}

// --- the direct algebra over backend::common::RenderF ---------------------

// Inherited attribute carrier, handed down through `project` below. Only the
// outline level travels: like the mpark backend and unlike the LaTeX one there
// is no `pnum_base` companion, because this backend numbers nothing.
struct RenderCtx {
    int level;
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
    // The stable name rides the heading as plain bracketed text, a
    // deliberate checkpoint call. No `:CUSTOM_ID:` drawer: an org
    // link target is what a *numbered* cross-reference would need, and nothing
    // in a fragment resolves one yet (the mpark backend's `{- .sref}` is the
    // analogous decision, made the other way only because that framework has a
    // srefs database to miss in).
    common::RenderF<Seeded> operator()(const ir::Section& s) const {
        const std::string stars(static_cast<std::size_t>(std::max(ctx.level, 1)), '*');
        // A hand-written Section may carry no title; emitting the empty one
        // would leave a double space before the stable name.
        std::string header = s.title.empty() ? std::format("{} [{}]\n", stars, s.stable_name)
                                             : std::format("{} {} [{}]\n", stars, s.title, s.stable_name);

        const RenderCtx     child_ctx{ctx.level + 1};
        std::vector<Seeded> children =
            s.children |
            std::views::transform([&child_ctx](const ir::Node& child) { return Seeded{&child, child_ctx}; }) |
            std::ranges::to<std::vector>();
        return common::RenderedSectionF<Seeded>{std::move(header), std::move(children)};
    }

    // The three leaves render independently of outline level, so they pass
    // through unchanged.
    common::RenderF<Seeded> operator()(const ir::Synopsis& v) const { return v; }
    common::RenderF<Seeded> operator()(const ir::SpecItem& v) const { return v; }
    common::RenderF<Seeded> operator()(const ir::FreeParagraph& v) const { return v; }
};

common::RenderF<Seeded> project(const Seeded& seeded) {
    return std::visit(overloaded{SeededProjector{seeded.ctx}}, *seeded.node);
}

// Dispatches one already-rendered RenderF layer to its org text. Four
// alternatives (decision visitation-rules' >3 rule), so a named visitor
// struct. The heading has
// already been resolved by `project`, so every case is a plain
// RenderF<std::string> -> std::string mapping.
struct OrgAlgebra {
    std::string operator()(const common::RenderedSectionF<std::string>& s) const {
        if (s.children.empty())
            return s.header;
        return s.header + "\n" + (s.children | std::views::join_with('\n') | std::ranges::to<std::string>());
    }

    // A synopsis is a `codeblock`, an itemdecl's signatures are an `itemdecl`
    // -- the same two environments the LaTeX backend picks, because they are
    // the same two environments.
    std::string operator()(const ir::Synopsis& v) const { return render_block("codeblock", v.code); }
    std::string operator()(const ir::SpecItem& v) const { return render_item(v); }
    std::string operator()(const ir::FreeParagraph& v) const { return render_paragraph(v.text) + '\n'; }
};

std::string render_layer(const common::RenderF<std::string>& layer) {
    return std::visit(overloaded{OrgAlgebra{}}, layer);
}

std::string render_node_to_string(const ir::Node& node, const RenderCtx& ctx) {
    return beman::tree_algorithms::fold_with<std::string>(
        render_layer, common::render_fmap, project, Seeded{&node, ctx});
}

} // namespace

std::string render_to_string(const ir::Document& doc, const Options& options) {
    const RenderCtx                ctx{options.base_heading_level};
    const std::vector<std::string> rendered =
        doc.nodes | std::views::transform([&ctx](const ir::Node& node) { return render_node_to_string(node, ctx); }) |
        std::ranges::to<std::vector>();
    return rendered | std::views::join_with('\n') | std::ranges::to<std::string>();
}

std::string render_to_string(const ir::SpecItem& item, const Options&) { return render_item(item); }

} // namespace beman::specgen::backend::org
