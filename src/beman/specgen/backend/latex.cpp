// src/beman/specgen/backend/latex.cpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The LaTeX backend, a direct algebra over backend::backend::RenderF
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
// backend's to repurpose -- see `backend/render_fold.hpp`). The algebra that
// follows is therefore a plain `RenderF<std::string> -> std::string`, with
// no per-node closures and no type erasure.

#include <beman/specgen/backend/latex.hpp>

#include <beman/specgen/backend/code_spans.hpp>
#include <beman/specgen/backend/draft_spans.hpp>
#include <beman/specgen/backend/render_fold.hpp>
#include <beman/specgen/foundation/enumeration.hpp>
#include <beman/specgen/foundation/overloaded.hpp>
#include <beman/specgen/ir.hpp>

#include <beman/tree_algorithms/recursion_schemes.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace beman::specgen::backend::latex {

namespace {

using beman::specgen::foundation::overloaded;

// --- span substitution: the shared substrate's escape hooks ---------------

// The escape function this backend plugs into `backend::render_code_spans`
// lives in `backend/draft_spans.hpp` as `draft_span_codeblock`, and the sharing is
// not tidying: the org backend emits the *same* bytes,
// because its `#+begin_codeblock`/`#+begin_itemdecl` special blocks export to
// the draft's own listings environments. Two backends writing one convention
// have to write it once, or the second silently drifts from the first.
// The prose half of the pair, `draft_span_prose`, is reached only through
// `backend::draft_code_inline` (and through `draft_span_codeblock` itself),
// so this backend never names it.
// The shared header says the rest.
using backend::draft_span_codeblock;

// Code text is the author's own tokens; it is never re-escaped. Only the
// spans are substituted, via the shared substrate (backend/code_spans.hpp)
// -- and the wording diff's change runs, each closed in the draft's own
// `@\added{...}@` with the spans inside it in their escaped form, since the
// run has opened the `@...@` already (docs/plans/wording-diff.md).
std::string render_code_codeblock(const ir::CodeText& code) {
    return backend::render_code_spans(
        code, draft_span_codeblock, backend::draft_span_escaped, backend::draft_change_codeblock);
}

// A marked node or element in the draft's own environment for it,
// `addedblock` / `removedblock` (macros.tex); an unmarked one as it is.
std::string wrap_block(ir::Change change, std::string text) {
    const std::optional<std::string_view> environment = backend::draft_change_environment(change);
    if (!environment)
        return text;
    return std::format(
        "\\begin{{{}}}\n{}{}\\end{{{}}}\n", *environment, text, text.ends_with('\n') ? "" : "\n", *environment);
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
    // above are (backend/draft_spans.hpp).
    std::string operator()(const ir::CodeInline& v) const { return backend::draft_code_inline(v); }

    // A cross-reference to another stable name.
    std::string operator()(const ir::RefInline& v) const { return "\\iref{" + v.stable_name + '}'; }

    // A library concept name, rendered with \libconcept rather than \tcode.
    std::string operator()(const ir::ConceptRef& v) const { return "\\libconcept{" + v.name + '}'; }

    // A changed run of prose (the wording diff's mark): the draft's own
    // `\added{...}` / `\removed{...}` from macros.tex, around the run
    // rendered as it would be unmarked. Defined after `render_run` below,
    // which dispatches the run's `PlainInline`s -- a different, non-recursive
    // variant -- through this same visitor's other four cases.
    std::string operator()(const ir::ChangedInline& run) const;
};

std::string render_run(const std::vector<ir::PlainInline>& inlines) {
    return inlines | std::views::transform([](const ir::PlainInline& piece) {
               return std::visit(overloaded{PieceRenderer{}}, piece);
           }) |
           std::views::join | std::ranges::to<std::string>();
}

std::string PieceRenderer::operator()(const ir::ChangedInline& run) const {
    return backend::draft_change_prose(run.change, render_run(run.inlines));
}

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

// \libtab2's flat two-column table (issue #74): the draft's own
// [fs.enum.file.type]-shaped tables (`\begin{floattable}{...}{...}{lp{4.5in}}`
// with plain `\lhdr`/`\rhdr` headers) are exactly what the real `\libtab2`
// macro wraps, at the same 4.5in data-column width `LibEffTab` uses.
std::string render_flat_table(const ir::Table1D& table) {
    std::string       out = std::format("\\begin{{libtab2}}{{{}}}{{{}}}{{lp{{4.5in}}}}{{{}}}{{{}}}\n",
                                        render_table_field(table.caption),
                                        table.stable_name,
                                        render_table_field(table.column1),
                                        render_table_field(table.column2));
    const std::string rows =
        table.rows | std::views::transform([](const ir::Table1DRow& row) {
            return std::format("{} &\n{} \\\\\n", render_table_field(row.cell1), render_table_field(row.cell2));
        }) |
        std::views::join_with(std::string_view{"\\rowsep\n\n"}) | std::ranges::to<std::string>();
    if (!rows.empty())
        out += '\n' + rows;
    return out + "\\end{libtab2}\n";
}

// --- items --------------------------------------------------------------

std::string render_index(const ir::IndexEntry& entry) {
    return foundation::match(
        entry.kind,
        foundation::when<ir::IndexKind::Global>(
            [&] { return std::format("\\indexlibraryglobal{{{}}}%\n", entry.name); }),
        foundation::when<ir::IndexKind::Constructor>(
            [&] { return std::format("\\indexlibraryctor{{{}}}%\n", entry.name); }),
        foundation::when<ir::IndexKind::Destructor>(
            [&] { return std::format("\\indexlibrarydtor{{{}}}%\n", entry.name); }),
        foundation::when<ir::IndexKind::Member>(
            [&] { return std::format("\\indexlibrarymember{{{}}}{{{}}}%\n", entry.name, entry.parent); }),
        foundation::when<ir::IndexKind::MemberX>(
            [&] { return std::format("\\indexlibrarymemberx{{{}}}{{{}}}%\n", entry.name, entry.parent); }),
        foundation::when<ir::IndexKind::MemberExpos>(
            [&] { return std::format("\\indexlibrarymemberexpos{{{}}}{{{}}}%\n", entry.name, entry.parent); }),
        foundation::when<ir::IndexKind::Zombie>(
            [&] { return std::format("\\indexlibraryzombie{{{}}}%\n", entry.name); }),
        foundation::when<ir::IndexKind::Misc>(
            [&] { return std::format("\\indexlibrarymisc{{{}}}{{{}}}%\n", entry.name, entry.parent); }));
}

// One block's rendering and paragraph-boundary policy. Lists and tables
// belong to the preceding numbered paragraph; an extracted body starts one.
struct RenderedBodyBlock {
    std::string text;
    bool        starts_paragraph = false;
    bool        inline_label     = false;
};

struct LeafBodyRenderer {
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
    return {render_paragraph(p.text) + '\n', !p.continuation, true};
}

RenderedBodyBlock LeafBodyRenderer::operator()(const ir::Itemize& list) const {
    const std::string items =
        list.items |
        std::views::transform([](const ir::Paragraph& p) { return "\\item " + render_paragraph(p) + '\n'; }) |
        std::views::join | std::ranges::to<std::string>();
    return {"\\begin{itemize}\n" + items + "\\end{itemize}\n", false, false};
}

RenderedBodyBlock LeafBodyRenderer::operator()(const ir::Table2D& t) const { return {render_table(t), false, false}; }

RenderedBodyBlock LeafBodyRenderer::operator()(const ir::Table1D& t) const {
    return {render_flat_table(t), false, false};
}

RenderedBodyBlock LeafBodyRenderer::operator()(const ir::EquivalentTo& e) const {
    return {"Equivalent to:\n" + render_codeblock(e.code), true, true};
}

RenderedBodyBlock LeafBodyRenderer::operator()(const ir::AuthoredCode& code) const {
    return {render_codeblock(code.code), false, false};
}

std::string render_informative_body(const ir::InformativeBody& body);
std::string render_informative_body(const ir::InformativeBody& body) {
    return body | std::views::transform([](const ir::InformativeBlock& block) {
               return wrap_block(ir::block_change(block), std::visit(overloaded{LeafBodyRenderer{}}, block).text);
           }) |
           std::views::join_with('\n') | std::ranges::to<std::string>();
}

struct BodyRenderer : LeafBodyRenderer {
    using LeafBodyRenderer::operator();
    RenderedBodyBlock operator()(const ir::Note& note) const;
    RenderedBodyBlock operator()(const ir::Example& example) const;
};

RenderedBodyBlock BodyRenderer::operator()(const ir::Note& note) const {
    return {std::format("\\begin{{note}}\n{}\\end{{note}}\n", render_informative_body(note.body)), false, false};
}

RenderedBodyBlock BodyRenderer::operator()(const ir::Example& example) const {
    return {
        std::format("\\begin{{example}}\n{}\\end{{example}}\n", render_informative_body(example.body)), false, false};
}

std::vector<std::string> body_blocks(const ir::DescriptionBody& body, std::optional<ir::ElementKind> kind);

std::vector<std::string> body_blocks(const ir::DescriptionBody& body, std::optional<ir::ElementKind> kind) {
    const std::string label  = kind ? std::format("\\{}\n", ir::element_name(*kind)) : std::string{};
    auto              blocks = std::ranges::fold_left(
        body, std::vector<std::string>{}, [&](std::vector<std::string> out, const ir::BodyBlock& block) {
            auto       rendered = std::visit(overloaded{BodyRenderer{}}, block);
            const bool first    = out.empty();
            // A marked block (the wording diff's) closes in its environment
            // alone: around its whole numbered paragraph when it opens one, so
            // the number is inside the mark, and around just itself when it
            // continues the paragraph before it.
            const ir::Change change = ir::block_change(block);
            if (first || rendered.starts_paragraph) {
                out.push_back(wrap_block(change, "\\pnum\n" + (first ? label : std::string{}) + rendered.text));
            } else {
                out.back() += wrap_block(change, rendered.text);
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

    if (!beman::specgen::ir::has_description(item.descr))
        return wrap_block(item.change, std::move(out));

    // A run of same-kind elements folds into one group (merge_element_group)
    // only when it shares one mark: an authored *Constraints* the diff added
    // beside a derived one it kept is two groups, each in its own block, at
    // the price of the label twice -- the honest rendering of that edit.
    // Joining a group's blocks first and the groups after is the same join
    // as joining every block flat, so an unmarked item renders as it did.
    std::vector<std::string> groups = body_blocks(item.descr.leading, std::nullopt);
    groups.append_range(item.descr.elements |
                        std::views::chunk_by([](const ir::DescriptionElement& a, const ir::DescriptionElement& b) {
                            return a.kind == b.kind && a.change == b.change;
                        }) |
                        std::views::transform([](auto&& group) {
                            return wrap_block(std::ranges::begin(group)->change,
                                              element_group_blocks(group) | std::views::join_with('\n') |
                                                  std::ranges::to<std::string>());
                        }));

    if (!out.empty())
        out += '\n';
    if (!item.decl.signatures.empty())
        out += "\\begin{itemdescr}\n";
    out += groups | std::views::join_with('\n') | std::ranges::to<std::string>();
    if (!item.decl.signatures.empty())
        out += "\\end{itemdescr}\n";
    return wrap_block(item.change, std::move(out));
}

// --- the direct algebra over backend::backend::RenderF ---------------------
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
    // frozen JSON field it must remain (see backend/render_fold.hpp).
    backend::RenderF<Seeded> operator()(const ir::Section& s) const {
        std::string         header = std::format("\\rSec{}[{}]{{{}}}\n", ctx.depth, s.stable_name, s.title);
        const RenderCtx     child_ctx{ctx.depth + 1, ctx.pnum_base};
        std::vector<Seeded> children =
            s.children |
            std::views::transform([&child_ctx](const ir::Node& child) { return Seeded{&child, child_ctx}; }) |
            std::ranges::to<std::vector>();
        return backend::RenderedSectionF<Seeded>{std::move(header), std::move(children), s.change};
    }

    // The three leaves render independently of depth, so -- exactly like
    // ir::node_project's own leaf cases -- they pass through unchanged.
    backend::RenderF<Seeded> operator()(const ir::Synopsis& v) const { return v; }
    backend::RenderF<Seeded> operator()(const ir::SpecItem& v) const { return v; }
    backend::RenderF<Seeded> operator()(const ir::FreeParagraph& v) const { return v; }
};

backend::RenderF<Seeded> project(const Seeded& seeded) {
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
    //
    // A marked section (the wording diff's) closes in its environment here,
    // where header and children are first seen together.
    std::string operator()(const backend::RenderedSectionF<std::string>& s) const {
        if (s.children.empty())
            return wrap_block(s.change, s.header);
        return wrap_block(
            s.change, s.header + "\n" + (s.children | std::views::join_with('\n') | std::ranges::to<std::string>()));
    }

    // A synopsis: its own codeblock, independent of section depth.
    std::string operator()(const ir::Synopsis& v) const { return wrap_block(v.change, render_codeblock(v.code)); }

    // A declared item: delegate to the item renderer, independent of depth.
    std::string operator()(const ir::SpecItem& v) const { return render_item(v); }

    // A free paragraph: one \pnum-numbered paragraph of prose. An elided one
    // (the wording diff's `items` context) is the ellipsis the draft's own
    // editing instructions write for wording left out, unnumbered.
    std::string operator()(const ir::FreeParagraph& v) const {
        if (v.change == ir::Change::Elided)
            return "[\\ldots]\n";
        return wrap_block(v.change, "\\pnum\n" + render_paragraph(v.text) + '\n');
    }
};

std::string render_layer(const backend::RenderF<std::string>& layer) {
    return std::visit(overloaded{LatexAlgebra{}}, layer);
}

std::string render_node_to_string(const ir::Node& node, const RenderCtx& ctx) {
    return beman::tree_algorithms::fold_with<std::string>(
        render_layer, backend::render_fmap, project, Seeded{&node, ctx});
}

} // namespace

// A top-level node rendered unmarked, beside the mark it carried: the
// document render closes the wrapper itself, so that a run of adjacent
// top-level nodes with one mark shares one environment -- a fragment that is
// wholly new is one `addedblock`, not one per node.
struct RenderedTop {
    ir::Change  change;
    std::string text;
};

std::string render_to_string(const ir::Document& doc, const Options& options) {
    const RenderCtx ctx{options.base_section_depth};
    // Paper mode is the wording diff with an empty baseline
    // (docs/plans/wording-diff.md): every top-level node marked added, and
    // the marks then render exactly as a diff's would.
    const std::optional<std::vector<ir::Node>> marked =
        options.paper_mode ? std::optional{ir::all_marked(doc.nodes, ir::Change::Added)} : std::nullopt;
    const std::vector<ir::Node>&   nodes = marked ? *marked : doc.nodes;
    const std::vector<RenderedTop> rendered =
        nodes | std::views::transform([&ctx](const ir::Node& node) {
            return RenderedTop{
                ir::change_of(node),
                render_node_to_string(
                    ir::with_change(node,
                                    ir::change_of(node) == ir::Change::Elided ? ir::Change::Elided : ir::Change::Same),
                    ctx)};
        }) |
        std::ranges::to<std::vector>();
    return rendered |
           std::views::chunk_by([](const RenderedTop& a, const RenderedTop& b) { return a.change == b.change; }) |
           std::views::transform([](auto&& chunk) {
               return wrap_block(std::ranges::begin(chunk)->change,
                                 chunk | std::views::transform(&RenderedTop::text) | std::views::join_with('\n') |
                                     std::ranges::to<std::string>());
           }) |
           std::views::join_with('\n') | std::ranges::to<std::string>();
}

std::string render_to_string(const ir::SpecItem& item, const Options& options) {
    ir::SpecItem copy = item;
    if (options.paper_mode)
        copy.change = ir::Change::Added;
    return render_item(copy);
}

} // namespace beman::specgen::backend::latex
