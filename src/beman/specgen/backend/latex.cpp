// src/beman/specgen/backend/latex.cpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The LaTeX backend, a direct algebra over backend::common::RenderF
// (decision backend-direct-algebra). Every case
// below returns already-rendered text; nothing writes to an ambient
// std::ostream at all, which is what lets separators become a property of
// `views::join_with` rather than a `bool& first` thread or a
// `&para == &container.front()` identity test.
//
// That holds all the way out (decision format-print-output):
// `render_to_string` is the backend's whole surface, and there is no
// `render(..., std::ostream&)` overload beside it, because the only thing
// one could do is insert the string this algebra has already produced.
//
// Section depth is an inherited attribute -- it flows down while the fold
// itself computes up -- but it is resolved entirely inside the projection
// (`Seeded`/`project` below), not carried by the algebra's result type: a
// Section's own `\rSecN` header depends on *this* node's depth, which is
// known the moment its parent hands it a child seed, so it is rendered
// during descent, into `RenderF`'s `RenderedSectionF::header` (an honestly
// named field, unlike `ir::SectionF::stable_name`, which is not this
// backend's to repurpose -- see `backend/common.hpp`). The algebra that
// follows is therefore a plain `RenderF<std::string> -> std::string`, with
// no per-node closures and no type erasure.

#include <beman/specgen/backend/latex.hpp>

#include <beman/specgen/backend/common.hpp>
#include <beman/specgen/foundation/overloaded.hpp>

#include <beman/tree_algorithms/recursion_schemes.hpp>

#include <format>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace beman::specgen::backend::latex {

namespace {

using beman::specgen::foundation::overloaded;

// --- span substitution: the shared substrate's escape hooks ---------------

// The escape function this backend plugs into `common::render_code_spans`
// lives in `backend/common.hpp` as `draft_span_codeblock`, and the sharing is
// not tidying: the org backend emits the *same* bytes,
// because its `#+begin_codeblock`/`#+begin_itemdecl` special blocks export to
// the draft's own listings environments. Two backends writing one convention
// have to write it once, or the second silently drifts from the first.
// The prose half of the pair, `draft_span_prose`, is reached only through
// `common::draft_code_inline` (and through `draft_span_codeblock` itself),
// so this backend never names it.
// The shared header says the rest.
using common::draft_span_codeblock;

// Code text is the author's own tokens; it is never re-escaped. Only the
// spans are substituted, via the shared substrate (backend/common.hpp).
std::string render_code_codeblock(const ir::CodeText& code) {
    return common::render_code_spans(code, draft_span_codeblock);
}

std::string render_codeblock(const ir::CodeText& code) {
    return "\\begin{codeblock}\n" + render_code_codeblock(code) + "\n\\end{codeblock}\n";
}

// --- prose ------------------------------------------------------------------

// Dispatches one Inline (a Paragraph's Piece) to its LaTeX prose rendering.
// Four alternatives (decision visitation-rules' >3 rule), so a named
// visitor struct rather than an overloaded-lambda set.
struct PieceRenderer {
    // Plain prose text, verbatim.
    std::string operator()(const ir::TextInline& v) const { return v.text; }

    // A code inline, with the whole-span shortcut the draft's macros need.
    // Shared with the org backend for the reason the span escapes
    // above are (backend/common.hpp).
    std::string operator()(const ir::CodeInline& v) const { return common::draft_code_inline(v); }

    // A cross-reference to another stable name.
    std::string operator()(const ir::RefInline& v) const { return "\\iref{" + v.stable_name + '}'; }

    // A library concept name, rendered with \libconcept rather than \tcode.
    std::string operator()(const ir::ConceptRef& v) const { return "\\libconcept{" + v.name + '}'; }
};

std::string render_paragraph(const ir::Paragraph& para) {
    return para | std::views::transform([](const ir::Inline& piece) {
               return std::visit(overloaded{PieceRenderer{}}, piece);
           }) |
           std::views::join | std::ranges::to<std::string>();
}

std::string render_table_field(const ir::Paragraph& paragraph) {
    return render_paragraph(paragraph) |
           std::views::transform([](const char ch) { return ch == '&' ? std::string{"\\&"} : std::string(1, ch); }) |
           std::views::join | std::ranges::to<std::string>();
}

std::string render_table(const ir::Table2D& table) {
    std::string       out  = std::format("\\begin{{lib2dtab2}}{{{}}}{{{}}}\n{{{}}}\n{{{}}}\n",
                                         render_table_field(table.caption),
                                         table.stable_name,
                                         render_table_field(table.column1),
                                         render_table_field(table.column2));
    const std::string rows = table.rows | std::views::transform([](const ir::Table2DRow& row) {
                                 return std::format("\\rowhdr{{{}}} &\n{} &\n{} \\\\\n",
                                                    render_table_field(row.header),
                                                    render_table_field(row.cell1),
                                                    render_table_field(row.cell2));
                             }) |
                             std::views::join_with(std::string_view{"\\rowsep\n\n"}) | std::ranges::to<std::string>();
    if (!rows.empty())
        out += '\n' + rows;
    return out + "\\end{lib2dtab2}\n";
}

// --- items --------------------------------------------------------------

std::string render_index(const ir::IndexEntry& entry) {
    switch (entry.kind) {
    case ir::IndexKind::Global:
        return std::format("\\indexlibraryglobal{{{}}}%\n", entry.name);
    case ir::IndexKind::Constructor:
        return std::format("\\indexlibraryctor{{{}}}%\n", entry.name);
    case ir::IndexKind::Destructor:
        return std::format("\\indexlibrarydtor{{{}}}%\n", entry.name);
    case ir::IndexKind::Member:
        return std::format("\\indexlibrarymember{{{}}}{{{}}}%\n", entry.name, entry.parent);
    case ir::IndexKind::MemberX:
        return std::format("\\indexlibrarymemberx{{{}}}{{{}}}%\n", entry.name, entry.parent);
    case ir::IndexKind::MemberExpos:
        return std::format("\\indexlibrarymemberexpos{{{}}}{{{}}}%\n", entry.name, entry.parent);
    case ir::IndexKind::Zombie:
        return std::format("\\indexlibraryzombie{{{}}}%\n", entry.name);
    case ir::IndexKind::Misc:
        return std::format("\\indexlibrarymisc{{{}}}{{{}}}%\n", entry.name, entry.parent);
    }
    std::unreachable(); // IndexKind is exhaustively handled above
}

// One DescriptionElement's \pnum blocks, in emission order. Each entry is
// one already-terminated \pnum unit; the caller joins these -- and every
// other element's blocks -- with a blank-line separator (decision
// backend-direct-algebra:
// separation is a property of the join, not a `bool& first` thread spanning
// the whole itemdescr).
//
// The element macro name (\constraints, \mandates, ...) is ElementKind's own
// spelling; it appears exactly once, on whichever block is emitted first.
std::vector<std::string> element_blocks(const ir::DescriptionElement&   element,
                                        const std::vector<ir::Table2D>& tables) {
    std::vector<std::string> blocks;
    const std::string        macro = "\\" + std::string(ir::element_name(element.kind)) + '\n';

    if (!element.paragraphs.empty()) {
        // The first paragraph's block carries the macro; the rest are bare
        // \pnum units -- named by position (front / drop(1)), not by a
        // pointer-identity test inside a loop.
        blocks.push_back("\\pnum\n" + macro + render_paragraph(element.paragraphs.front()) + '\n');
        blocks.append_range(element.paragraphs | std::views::drop(1) |
                            std::views::transform(
                                [](const ir::Paragraph& para) { return "\\pnum\n" + render_paragraph(para) + '\n'; }));
    }

    if (element.itemize) {
        const std::string items        = element.itemize->items | std::views::transform([](const ir::Paragraph& item) {
                                      return "\\item " + render_paragraph(item) + '\n';
                                         }) |
                                         std::views::join | std::ranges::to<std::string>();
        const std::string itemize_text = "\\begin{itemize}\n" + items + "\\end{itemize}\n";

        // A list belongs to the numbered paragraph it enumerates: it follows
        // the lead-in prose (folded into that paragraph's own block, no new
        // \pnum) when one exists, or opens its own \pnum block -- with the
        // macro, since it is then the element's only content -- when it is
        // the whole content.
        if (blocks.empty())
            blocks.push_back("\\pnum\n" + macro + itemize_text);
        else
            blocks.back() += itemize_text;
    }

    const std::string rendered_tables =
        tables | std::views::transform(render_table) | std::views::join | std::ranges::to<std::string>();
    if (!rendered_tables.empty()) {
        if (blocks.empty())
            blocks.push_back("\\pnum\n" + macro + rendered_tables);
        else
            blocks.back() += rendered_tables;
    }

    if (element.equivalent) {
        // The macro is written once per element, by whichever block comes
        // first; "Equivalent to:" always opens a fresh \pnum of its own.
        const bool needs_macro = blocks.empty();
        blocks.push_back("\\pnum\n" + (needs_macro ? macro : std::string{}) + "Equivalent to:\n" +
                         render_codeblock(element.equivalent->code));
    }

    return blocks;
}

// (design §5.2): an authored `\mandates`/`\constraints` element and
// code's derived twin are one description in the draft's eyes, not two --
// `ir::canonicalize` (ir.cpp) already sorts the derived one first within its
// kind ("derived conjuncts first, authored prose appended"), so folding a
// run of same-kind elements into one before `element_blocks` sees it is
// enough to get a single macro emission with the derived conjuncts leading
// and the authored prose appended, with no `element_blocks` change at all.
// A run of length 1 -- every kind but Constraints/Mandates, and either of
// those without an authored/derived pair -- copies its one element through
// unchanged.
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
    std::string out =
        item.decl.index | std::views::transform(render_index) | std::views::join | std::ranges::to<std::string>();

    // No signatures means a description with no itemdecl -- a class's own
    // wording (design §6), which the draft writes as bare paragraphs in a
    // general subclause. An empty `itemdecl` environment would be the same
    // blank box design §9 rejects on a synopsis.
    if (!item.decl.signatures.empty()) {
        out += "\\begin{itemdecl}\n";
        out += item.decl.signatures |
               std::views::transform([](const ir::CodeText& sig) { return render_code_codeblock(sig) + '\n'; }) |
               std::views::join | std::ranges::to<std::string>();
        out += "\\end{itemdecl}\n";
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
    out += "\\begin{itemdescr}\n";
    out += blocks | std::views::join_with('\n') | std::ranges::to<std::string>();
    out += "\\end{itemdescr}\n";
    return out;
}

// --- the direct algebra over backend::common::RenderF ---------------------
// (decision backend-direct-algebra)

// Inherited attribute carrier, handed down through `project` below (not
// through the algebra's result type -- see `Seeded`). `pnum_base` is never
// read anywhere in this file: LaTeX's `\pnum` is self-numbering. It is
// nonetheless incremented alongside depth on every descent, purely so this
// carrier's *shape* already matches what a backend's
// paragraph counter would need; its presence here means only "reserved for a
// future consumer of this carrier" and nothing else should be inferred from
// it.
struct RenderCtx {
    int depth;
    int pnum_base = 0;
};

// The fold's seed/handle type: a node paired with the RenderCtx its parent
// assigned it. This is what resolves depth *before* any node is visited,
// rather than threading it through the algebra's result type: by the time
// `project` unwraps a `Seeded`, it already knows that node's own depth.
struct Seeded {
    const ir::Node* node;
    RenderCtx       ctx;
};

// `Seeded`'s std::visit dispatch (decision visitation-rules: named struct,
// ir::Node has four
// alternatives), member state (`ctx`) rather than a capture -- the same
// shape ir_fold.hpp's own NodeProjector uses for this variant.
struct SeededProjector {
    RenderCtx ctx;

    // A subsection: this node's own `\rSecN[...]{...}` header depends on
    // `ctx.depth`, which is only available here, during descent -- not
    // later inside the algebra, which sees only an already-completed
    // RenderedSectionF<std::string> with no ambient context. So the header
    // is rendered now, into `RenderedSectionF::header` -- a field named for
    // exactly this, unlike `ir::SectionF::stable_name`, which stays the
    // frozen JSON field it must remain (see backend/common.hpp).
    common::RenderF<Seeded> operator()(const ir::Section& s) const {
        std::string         header = std::format("\\rSec{}[{}]{{{}}}\n", ctx.depth, s.stable_name, s.title);
        const RenderCtx     child_ctx{ctx.depth + 1, ctx.pnum_base};
        std::vector<Seeded> children =
            s.children |
            std::views::transform([&child_ctx](const ir::Node& child) { return Seeded{&child, child_ctx}; }) |
            std::ranges::to<std::vector>();
        return common::RenderedSectionF<Seeded>{std::move(header), std::move(children)};
    }

    // The three leaves render independently of depth, so -- exactly like
    // ir::node_project's own leaf cases -- they pass through unchanged.
    common::RenderF<Seeded> operator()(const ir::Synopsis& v) const { return v; }
    common::RenderF<Seeded> operator()(const ir::SpecItem& v) const { return v; }
    common::RenderF<Seeded> operator()(const ir::FreeParagraph& v) const { return v; }
};

common::RenderF<Seeded> project(const Seeded& seeded) {
    return std::visit(overloaded{SeededProjector{seeded.ctx}}, *seeded.node);
}

// Dispatches one already-rendered RenderF layer to its LaTeX text. Four
// alternatives (decision visitation-rules' >3 rule), so a named visitor
// struct. Depth has
// already been resolved by `project`, so every case here is a plain
// RenderF<std::string> -> std::string mapping: no context, no closures, no
// per-node allocation.
struct LatexAlgebra {
    // A subsection: `s.header` is the text `project` already rendered for
    // this node (see SeededProjector); join the already-rendered children
    // underneath it with a blank-line separator (the algebra's join-based
    // separation -- no `bool& first`, no recursion parameter threading
    // depth, no front-identity test).
    std::string operator()(const common::RenderedSectionF<std::string>& s) const {
        if (s.children.empty())
            return s.header;
        return s.header + "\n" + (s.children | std::views::join_with('\n') | std::ranges::to<std::string>());
    }

    // A synopsis: its own codeblock, independent of section depth.
    std::string operator()(const ir::Synopsis& v) const { return render_codeblock(v.code); }

    // A declared item: delegate to the item renderer, independent of depth.
    std::string operator()(const ir::SpecItem& v) const { return render_item(v); }

    // A free paragraph: one \pnum-numbered paragraph of prose.
    std::string operator()(const ir::FreeParagraph& v) const { return "\\pnum\n" + render_paragraph(v.text) + '\n'; }
};

std::string render_layer(const common::RenderF<std::string>& layer) {
    return std::visit(overloaded{LatexAlgebra{}}, layer);
}

std::string render_node_to_string(const ir::Node& node, const RenderCtx& ctx) {
    return beman::tree_algorithms::fold_with<std::string>(
        render_layer, common::render_fmap, project, Seeded{&node, ctx});
}

} // namespace

std::string render_to_string(const ir::Document& doc, const Options& options) {
    const RenderCtx                ctx{options.base_section_depth};
    const std::vector<std::string> rendered =
        doc.nodes | std::views::transform([&ctx](const ir::Node& node) { return render_node_to_string(node, ctx); }) |
        std::ranges::to<std::vector>();
    return rendered | std::views::join_with('\n') | std::ranges::to<std::string>();
}

std::string render_to_string(const ir::SpecItem& item, const Options&) { return render_item(item); }

} // namespace beman::specgen::backend::latex
