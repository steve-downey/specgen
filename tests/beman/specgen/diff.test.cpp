// tests/beman/specgen/diff.test.cpp                                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/diff.hpp>
#include <beman/specgen/diff.hpp> // Re-inclusion verification

#include <beman/specgen/draft_import.hpp>
#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>
#include <string>
#include <variant>
#include <vector>

using namespace beman::specgen::ir;
namespace diff         = beman::specgen::diff;
namespace draft_import = beman::specgen::draft_import;

namespace {

Paragraph text(std::string s) { return {TextInline{std::move(s)}}; }

DescriptionElement element(ElementKind kind, DescriptionBody body) {
    DescriptionElement e;
    e.kind = kind;
    e.body = std::move(body);
    return e;
}

SpecItem item(std::string signature, std::vector<std::string> entities, std::vector<DescriptionElement> elements) {
    SpecItem it;
    it.decl.signatures.push_back({std::move(signature), {}});
    it.decl.entities  = std::move(entities);
    it.descr.elements = std::move(elements);
    return it;
}

// The small document the laws are checked on: a synopsis, a section holding
// two items and a free paragraph, and a second section.
Document gadget() {
    Document doc;
    doc.nodes.push_back(
        Synopsis{.name = "gadget", .code = {"class gadget {\n  bool ready() const;\n};", {}}, .roster = {}});
    doc.nodes.push_back(Section{
        "gadget.observe",
        "Observers",
        {item("bool ready() const;",
              {"ready"},
              {element(ElementKind::Returns,
                       {BodyParagraph{{CodeInline{{"true", {}}}, TextInline{" if the gadget is ready."}}}})}),
         item(
             "void reset();", {"reset"}, {element(ElementKind::Effects, {BodyParagraph{text("Resets the gadget.")}})}),
         FreeParagraph{text("A free paragraph.")}}});
    doc.nodes.push_back(
        Section{"gadget.mod",
                "Modifiers",
                {item("void arm();", {"arm"}, {element(ElementKind::Effects, {BodyParagraph{text("Arms it.")}})})}});
    return doc;
}

// --- stripping, for the restoration law --------------------------------------
// Keep one side of every mark: the `keep` change's material and everything
// unmarked, with the marks removed; the other side's material is dropped.

Paragraph strip_paragraph(const Paragraph& p, Change keep) {
    Paragraph out;
    for (const Inline& piece : p) {
        if (const auto* run = std::get_if<ChangedInline>(&piece)) {
            if (run->change != keep)
                continue;
            for (const PlainInline& inner : run->inlines) {
                Inline widened = std::visit([](auto v) -> Inline { return v; }, inner);
                auto*  t       = std::get_if<TextInline>(&widened);
                auto*  last    = out.empty() ? nullptr : std::get_if<TextInline>(&out.back());
                if (t != nullptr && last != nullptr)
                    last->text += t->text;
                else
                    out.push_back(std::move(widened));
            }
            continue;
        }
        auto* t    = std::get_if<TextInline>(&piece);
        auto* last = out.empty() ? nullptr : std::get_if<TextInline>(&out.back());
        if (t != nullptr && last != nullptr)
            last->text += t->text;
        else
            out.push_back(piece);
    }
    return out;
}

CodeText strip_code(const CodeText& c, Change keep) {
    CodeText    out;
    std::size_t pos = 0;
    for (const ChangeRun& run : c.changes) {
        out.text += c.text.substr(pos, run.begin - pos);
        if (run.change == keep)
            out.text += c.text.substr(run.begin, run.end - run.begin);
        pos = run.end;
    }
    out.text += c.text.substr(pos);
    // Lines emptied by the strip (a wholly removed or added line) leave a
    // blank line behind; the originals never had one, so drop those.
    std::string joined;
    for (auto&& line : out.text | std::views::split('\n')) {
        const std::string_view l(line.begin(), line.end());
        if (l.empty() && !c.changes.empty())
            continue;
        joined += std::string(l) + '\n';
    }
    if (!joined.empty())
        joined.pop_back();
    out.text = joined;
    return out; // spans are not compared by the law
}

BodyParagraph strip_block(const BodyParagraph& b, Change keep) {
    return {strip_paragraph(b.text, keep), b.continuation};
}

template <class Block>
std::vector<Block> strip_blocks(const std::vector<Block>& body, Change keep) {
    std::vector<Block> out;
    for (const Block& block : body) {
        if (block_change(block) != Change::Same && block_change(block) != keep)
            continue;
        std::visit(
            [&](auto b) {
                b.change = Change::Same;
                if constexpr (std::is_same_v<decltype(b), BodyParagraph>)
                    b = strip_block(b, keep);
                out.push_back(std::move(b));
            },
            block);
    }
    return out;
}

std::vector<Node> strip_nodes(const std::vector<Node>& nodes, Change keep);

Node strip_node(const Node& node, Change keep) {
    return std::visit(
        [&](auto n) -> Node {
            n.change = Change::Same;
            if constexpr (std::is_same_v<decltype(n), Section>) {
                n.children = strip_nodes(n.children, keep);
            } else if constexpr (std::is_same_v<decltype(n), Synopsis>) {
                n.code = strip_code(n.code, keep);
            } else if constexpr (std::is_same_v<decltype(n), SpecItem>) {
                std::vector<CodeText> sigs;
                for (const CodeText& s : n.decl.signatures) {
                    CodeText stripped = strip_code(s, keep);
                    if (!stripped.text.empty())
                        sigs.push_back(std::move(stripped));
                }
                n.decl.signatures = std::move(sigs);
                std::vector<DescriptionElement> elements;
                for (const DescriptionElement& e : n.descr.elements) {
                    if (e.change != Change::Same && e.change != keep)
                        continue;
                    DescriptionElement stripped = e;
                    stripped.change             = Change::Same;
                    stripped.body               = strip_blocks(e.body, keep);
                    elements.push_back(std::move(stripped));
                }
                n.descr.elements = std::move(elements);
            } else {
                n.text = strip_paragraph(n.text, keep);
            }
            return n;
        },
        node);
}

std::vector<Node> strip_nodes(const std::vector<Node>& nodes, Change keep) {
    std::vector<Node> out;
    for (const Node& node : nodes) {
        const Change c = change_of(node);
        if (c != Change::Same && c != keep)
            continue;
        out.push_back(strip_node(node, keep));
    }
    return out;
}

// Signatures and synopsis code are compared by text only after a strip (the
// strip does not reconstruct spans), and with whitespace removed: a kept
// token takes the current revision's spacing, so the law holds up to that.
std::string code_texts(const Document& d) {
    std::string out;
    for (const Node& n : d.nodes) {
        if (const auto* s = std::get_if<Synopsis>(&n))
            out += s->code.text + "\n--\n";
        if (const auto* sec = std::get_if<Section>(&n))
            for (const Node& child : sec->children)
                if (const auto* it = std::get_if<SpecItem>(&child))
                    for (const CodeText& sig : it->decl.signatures)
                        out += sig.text + "\n--\n";
    }
    std::erase_if(out, [](char ch) { return ch == ' ' || ch == '\t'; });
    return out;
}

} // namespace

TEST_CASE("diff - HeaderIsIdempotent") { REQUIRE(true); }

TEST_CASE("diff - context names round-trip") {
    for (const diff::Context c :
         {diff::Context::Changed, diff::Context::All, diff::Context::Items, diff::Context::Decls}) {
        const auto back = diff::context_from_name(diff::context_name(c));
        REQUIRE(back.has_value());
        CHECK(*back == c);
    }
    CHECK(!diff::context_from_name("some").has_value());
}

TEST_CASE("diff - identity law: a document against itself carries no marks and is itself under All") {
    const Document d   = gadget();
    const Document out = diff::compare(d, d, {.context = diff::Context::All});
    CHECK(out == d);
    CHECK(!diff::has_marks(out));
    // Under the default context nothing changed, so nothing is kept.
    CHECK(diff::compare(d, d).nodes.empty());
}

TEST_CASE("diff - empty-baseline law: every top-level node is added and nothing beneath") {
    const Document d   = gadget();
    const Document out = diff::compare(Document{}, d);
    REQUIRE(out.nodes.size() == d.nodes.size());
    for (const Node& n : out.nodes)
        CHECK(change_of(n) == Change::Added);
    const Document marked_by_hand{all_marked(d.nodes, Change::Added)};
    CHECK(out.nodes == marked_by_hand.nodes);
}

TEST_CASE("diff - a renamed member is an item removed and an item added; a changed signature is one item") {
    Document before  = gadget();
    Document after   = gadget();
    auto&    section = std::get<Section>(after.nodes[1]);
    // `reset` becomes `clear`: a new entity, so removal plus addition.
    auto& reset                   = std::get<SpecItem>(section.children[1]);
    reset.decl.signatures[0].text = "void clear();";
    reset.decl.entities           = {"clear"};
    // `ready` gains noexcept: the same entity, so one item with a code run.
    auto& ready                   = std::get<SpecItem>(section.children[0]);
    ready.decl.signatures[0].text = "bool ready() const noexcept;";

    const Document out = diff::compare(before, after);
    REQUIRE(out.nodes.size() == 1); // the synopsis and gadget.mod did not change
    const auto& observe = std::get<Section>(out.nodes[0]);
    CHECK(observe.stable_name == "gadget.observe");
    REQUIRE(observe.children.size() == 4);
    const auto& kept = std::get<SpecItem>(observe.children[0]);
    CHECK(kept.change == Change::Same);
    CHECK(kept.decl.signatures[0].text == "bool ready() const noexcept;");
    REQUIRE(kept.decl.signatures[0].changes.size() == 1);
    CHECK(kept.decl.signatures[0].changes[0].change == Change::Added);
    CHECK(kept.decl.signatures[0].text.substr(kept.decl.signatures[0].changes[0].begin,
                                              kept.decl.signatures[0].changes[0].end -
                                                  kept.decl.signatures[0].changes[0].begin) == "noexcept");
    CHECK(std::get<SpecItem>(observe.children[1]).change == Change::Removed);
    CHECK(std::get<SpecItem>(observe.children[1]).decl.entities == std::vector<std::string>{"reset"});
    CHECK(std::get<SpecItem>(observe.children[2]).change == Change::Added);
    CHECK(std::get<SpecItem>(observe.children[2]).decl.entities == std::vector<std::string>{"clear"});
    CHECK(std::get<FreeParagraph>(observe.children[3]).change == Change::Same);
}

TEST_CASE("diff - a reworded paragraph is marked word by word, code inlines whole") {
    Document before = gadget();
    Document after  = gadget();
    auto&    ready  = std::get<SpecItem>(std::get<Section>(after.nodes[1]).children[0]);
    std::get<BodyParagraph>(ready.descr.elements[0].body[0]).text = {CodeInline{{"true", {}}},
                                                                     TextInline{" if the gadget is armed."}};
    const Document out                                            = diff::compare(before, after);
    const auto&    para                                           = std::get<BodyParagraph>(
        std::get<SpecItem>(std::get<Section>(out.nodes[0]).children[0]).descr.elements[0].body[0]);
    REQUIRE(para.text.size() == 4);
    CHECK(std::holds_alternative<CodeInline>(para.text[0]));
    CHECK(std::get<TextInline>(para.text[1]).text == " if the gadget is ");
    const auto& removed = std::get<ChangedInline>(para.text[2]);
    const auto& added   = std::get<ChangedInline>(para.text[3]);
    CHECK(removed.change == Change::Removed);
    CHECK(std::get<TextInline>(removed.inlines[0]).text == "ready.");
    CHECK(added.change == Change::Added);
    CHECK(std::get<TextInline>(added.inlines[0]).text == "armed.");
}

TEST_CASE("diff - a word and a table reference of one spelling are not the same token for pairing") {
    // Issue #453: the similarity keys for the two kinds had one prefix, so a
    // paragraph that is a table reference paired with one that is the word.
    Document   before     = gadget();
    Document   after      = gadget();
    const auto reset_body = [](Document& d) -> DescriptionBody& {
        return std::get<SpecItem>(std::get<Section>(d.nodes[1]).children[1]).descr.elements[0].body;
    };
    // A second paragraph in each, since a lone block pairs with a lone block
    // of its kind whatever the two say.
    reset_body(before).push_back(BodyParagraph{{TableRef{"gadget.states"}}});
    reset_body(after).push_back(BodyParagraph{{TextInline{"gadget.states"}}});

    const Document out  = diff::compare(before, after);
    const auto&    body = std::get<SpecItem>(std::get<Section>(out.nodes[0]).children[1]).descr.elements[0].body;
    REQUIRE(body.size() == 3);
    CHECK(block_change(body[0]) == Change::Same);
    CHECK(block_change(body[1]) == Change::Removed);
    CHECK(block_change(body[2]) == Change::Added);
}

TEST_CASE("diff - an added element, a removed section, and an added paragraph in a kept element") {
    Document before  = gadget();
    Document after   = gadget();
    auto&    observe = std::get<Section>(after.nodes[1]);
    auto&    ready   = std::get<SpecItem>(observe.children[0]);
    ready.descr.elements.push_back(element(ElementKind::Remarks, {BodyParagraph{text("New remark.")}}));
    auto& reset = std::get<SpecItem>(observe.children[1]);
    reset.descr.elements[0].body.push_back(BodyParagraph{text("A second paragraph.")});
    after.nodes.erase(after.nodes.begin() + 2); // gadget.mod goes

    const Document out = diff::compare(before, after);
    REQUIRE(out.nodes.size() == 2);
    const auto& sec       = std::get<Section>(out.nodes[0]);
    const auto& ready_out = std::get<SpecItem>(sec.children[0]);
    REQUIRE(ready_out.descr.elements.size() == 2);
    CHECK(ready_out.descr.elements[0].change == Change::Same);
    CHECK(ready_out.descr.elements[1].change == Change::Added);
    const auto& reset_out = std::get<SpecItem>(sec.children[1]);
    REQUIRE(reset_out.descr.elements[0].body.size() == 2);
    CHECK(block_change(reset_out.descr.elements[0].body[0]) == Change::Same);
    CHECK(block_change(reset_out.descr.elements[0].body[1]) == Change::Added);
    CHECK(std::get<Section>(out.nodes[1]).change == Change::Removed);
    CHECK(std::get<Section>(out.nodes[1]).stable_name == "gadget.mod");
}

TEST_CASE("diff - the items context elides unchanged children of a changed section") {
    Document before                                   = gadget();
    Document after                                    = gadget();
    auto&    observe                                  = std::get<Section>(after.nodes[1]);
    std::get<FreeParagraph>(observe.children[2]).text = text("A changed free paragraph.");
    const Document out = diff::compare(before, after, {.context = diff::Context::Items});
    REQUIRE(out.nodes.size() == 1);
    const auto& sec = std::get<Section>(out.nodes[0]);
    REQUIRE(sec.children.size() == 2);
    CHECK(std::get<FreeParagraph>(sec.children[0]).change == Change::Elided);
    CHECK(std::get<FreeParagraph>(sec.children[1]).change == Change::Same);
    CHECK(diff::has_marks(out));
}

TEST_CASE("diff - a code line added, one removed, one changed in a synopsis") {
    Document before                              = gadget();
    Document after                               = gadget();
    std::get<Synopsis>(after.nodes[0]).code.text = "class gadget {\n  bool ready() const noexcept;\n  void arm();\n};";
    const Document out                           = diff::compare(before, after);
    REQUIRE(out.nodes.size() == 1);
    const auto& code = std::get<Synopsis>(out.nodes[0]).code;
    CHECK(code.text == "class gadget {\n  bool ready() const noexcept;\n  void arm();\n};");
    REQUIRE(code.changes.size() == 2);
    CHECK(code.text.substr(code.changes[0].begin, code.changes[0].end - code.changes[0].begin) == "noexcept");
    CHECK(code.changes[0].change == Change::Added);
    CHECK(code.text.substr(code.changes[1].begin, code.changes[1].end - code.changes[1].begin) == "void arm();");
    CHECK(code.changes[1].change == Change::Added);
    // No run crosses a line end.
    for (const ChangeRun& run : code.changes)
        CHECK(code.text.substr(run.begin, run.end - run.begin).find('\n') == std::string::npos);
}

// A `\verbatim-synopsis` keeps its text as typed, `// \ref{x}` in a comment
// included, where the importer reads the draft's comment as a reference span.
// The same bytes are the same wording (issue #392).
TEST_CASE("diff - a reference in an authored listing's comment is the draft's reference span") {
    const std::string typed = "namespace std {\n  // \\ref{demo.thing}, class \\tcode{thing}\n  class thing;\n}";
    Document          before;
    before.nodes.push_back(Synopsis{.name = "thing", .code = draft_import::parse_listing(typed), .roster = {}});
    REQUIRE(!std::get<Synopsis>(before.nodes[0]).code.spans.empty());
    Document after;
    after.nodes.push_back(Synopsis{.name = "", .code = {typed, {}}, .roster = {}});
    CHECK(!diff::has_marks(diff::compare(before, after, {.context = diff::Context::All})));
}

// Which of a macro's two forms the draft wrote is typesetting (issue #306). A
// baseline that spells an exposition-only name `\exposid` and a revision that
// spells it `\exposidnc` say the same thing.
TEST_CASE("diff - the form of a span's macro is not a change, and the current revision's is kept") {
    const auto spanned = [](std::optional<bool> span_nc, bool inline_nc) {
        Document d = gadget();
        // "class gadget {\n  bool ready() const;\n};": `ready` is bytes [22, 27).
        std::get<Synopsis>(d.nodes[0]).code.spans = {{22, 27, SpanKind::ExposId, "ready", span_nc}};
        auto& ready                               = std::get<SpecItem>(std::get<Section>(d.nodes[1]).children[0]);
        std::get<BodyParagraph>(ready.descr.elements[0].body[0]).text = {
            CodeInline{{"ready", {{0, 5, SpanKind::ExposId, "ready", span_nc}}}},
            TextInline{" for "},
            PlaceholderInline{.text = "p", .no_correction = inline_nc},
            TextInline{" if the gadget is ready."}};
        return d;
    };
    const Document before = spanned(std::nullopt, false);
    Document       after  = spanned(false, true);
    CHECK(before != after);
    const Document same = diff::compare(before, after, {.context = diff::Context::All});
    CHECK(!diff::has_marks(same));
    CHECK(same == after);

    // A real change on the same line is still found, alone, and the span beside
    // it keeps the form the current revision gave it.
    std::get<Synopsis>(after.nodes[0]).code.text = "class gadget {\n  bool ready() const noexcept;\n};";
    const Document out                           = diff::compare(before, after, {.context = diff::Context::All});
    const auto&    code                          = std::get<Synopsis>(out.nodes[0]).code;
    REQUIRE(code.changes.size() == 1);
    CHECK(code.text.substr(code.changes[0].begin, code.changes[0].end - code.changes[0].begin) == "noexcept");
    REQUIRE(code.spans.size() == 1);
    CHECK(code.spans[0] == Span{22, 27, SpanKind::ExposId, "ready", false});
}

// An index entry around a name is not wording (issue #354): the draft writes a
// class head plain where an extracted synopsis writes `\libglobal{gadget}`.
TEST_CASE("diff - an index entry around a name is not a change, and the current revision's is kept") {
    const Document before = gadget();
    Document       after  = gadget();
    // "class gadget {\n  bool ready() const;\n};": `gadget` is bytes [6, 12).
    std::get<Synopsis>(after.nodes[0]).code.spans = {{6, 12, SpanKind::LibraryIndex, ""}};
    const Document same                           = diff::compare(before, after, {.context = diff::Context::All});
    CHECK(!diff::has_marks(same));
    CHECK(same == after);

    // A real change on the line is found alone, and the entry is kept.
    std::get<Synopsis>(after.nodes[0]).code.text = "class gadget final {\n  bool ready() const;\n};";
    const Document out                           = diff::compare(before, after, {.context = diff::Context::All});
    const auto&    code                          = std::get<Synopsis>(out.nodes[0]).code;
    REQUIRE(code.changes.size() == 1);
    CHECK(code.text.substr(code.changes[0].begin, code.changes[0].end - code.changes[0].begin) == "final");
    REQUIRE(code.spans.size() == 1);
    CHECK(code.spans[0].kind == SpanKind::LibraryIndex);
    CHECK(code.text.substr(code.spans[0].begin, code.spans[0].end - code.spans[0].begin) == "gadget");
}

// The draft's line-break hints are typesetting too. Code that differs only in
// them is the same wording, and comes out as the current revision has it.
TEST_CASE("diff - a line-break hint is not a change, and the current revision's hints are kept") {
    const auto hinted = [](bool with_hints) {
        Document d = gadget();
        // "class gadget {\n  bool ready() const;\n};": `ready` is bytes [22, 27).
        auto& synopsis = std::get<Synopsis>(d.nodes[0]).code;
        synopsis.spans = {{22, 27, SpanKind::ExposId, "ready"}};
        auto&    ready = std::get<SpecItem>(std::get<Section>(d.nodes[1]).children[0]);
        CodeText code{"std::forward<Args>(args)", {}};
        if (with_hints) {
            synopsis.hints = {{24, LayoutHintKind::Hyphen}};
            code.hints     = {{18, LayoutHintKind::Break, true}};
        }
        std::get<BodyParagraph>(ready.descr.elements[0].body[0]).text = {CodeInline{std::move(code)},
                                                                         TextInline{" if the gadget is ready."}};
        return d;
    };
    const Document before = hinted(false);
    const Document after  = hinted(true);
    CHECK(before != after);
    const Document out = diff::compare(before, after, {.context = diff::Context::All});
    CHECK(!diff::has_marks(out));
    CHECK(out == after);
    // And the other way: a revision that drops the hints has not changed the wording.
    CHECK(diff::compare(after, before, {.context = diff::Context::All}) == before);
}

// And in prose (issue #314): the hints in running text and in a prose inline's
// name are beside the wording. Text that differs only in them is the same
// wording; and where a word did change, the words that did not keep the hints
// the current revision has in and beside them.
TEST_CASE("diff - a line-break hint in prose is not a change, and a kept word keeps its hints") {
    const auto hinted = [](bool with_hints, std::string_view verb) {
        Document  d     = gadget();
        auto&     reset = std::get<SpecItem>(std::get<Section>(d.nodes[1]).children[1]);
        Paragraph para{TextInline{std::string(verb) + " the implementation, for "},
                       OldConceptRef{"UnaryTypeTrait"},
                       TextInline{" and "},
                       CodeInline{{"x", {}}},
                       TextInline{"."}};
        if (with_hints) {
            const std::size_t word                 = verb.size() + 5; // the start of "implementation"
            std::get<TextInline>(para[0]).hints    = {{word + 5, LayoutHintKind::Hyphen},
                                                      {verb.size() + 25, LayoutHintKind::LineBreak}};
            std::get<OldConceptRef>(para[1]).hints = {{5, LayoutHintKind::Hyphen}};
            std::get<TextInline>(para[2]).hints    = {{5, LayoutHintKind::Break, true}};
        }
        std::get<BodyParagraph>(reset.descr.elements[0].body[0]).text = std::move(para);
        return d;
    };
    const auto para_of = [](const Document& d) {
        return std::get<BodyParagraph>(
                   std::get<SpecItem>(std::get<Section>(d.nodes[0]).children[1]).descr.elements[0].body[0])
            .text;
    };
    const Document before = hinted(false, "Resets");
    const Document after  = hinted(true, "Resets");
    CHECK(before != after);
    const Document same = diff::compare(before, after, {.context = diff::Context::All});
    CHECK(!diff::has_marks(same));
    CHECK(same == after);
    CHECK(diff::compare(after, before, {.context = diff::Context::All}) == before);

    // One word replaced: the mark is on that word, and the rest of the
    // paragraph is the current revision's, hints and all.
    const Document  changed = hinted(true, "Clears");
    const Paragraph para    = para_of(diff::compare(before, changed));
    REQUIRE(para.size() == 7);
    CHECK(std::get<ChangedInline>(para[0]).inlines == std::vector<PlainInline>{TextInline{"Resets"}});
    CHECK(std::get<ChangedInline>(para[1]).inlines == std::vector<PlainInline>{TextInline{"Clears"}});
    CHECK(std::get<TextInline>(para[2]) ==
          TextInline{" the implementation, for ", {{10, LayoutHintKind::Hyphen}, {25, LayoutHintKind::LineBreak}}});
    CHECK(std::get<OldConceptRef>(para[3]).hints == std::vector<LayoutHint>{{5, LayoutHintKind::Hyphen}});
    CHECK(std::get<TextInline>(para[4]) == TextInline{" and ", {{5, LayoutHintKind::Break, true}}});
}

// The draft's tie (issue #312) is a hint on a space. A baseline that ties a
// word to what follows and a revision that writes a plain space say the same
// thing; and where the tied space falls at the edge of a mark, the tie goes
// with the space.
TEST_CASE("diff - a tie is not a change, and stays on its space beside a changed word") {
    const auto tied = [](bool with_tie, std::string_view noun) {
        Document  d     = gadget();
        auto&     reset = std::get<SpecItem>(std::get<Section>(d.nodes[1]).children[1]);
        Paragraph para{TextInline{"See " + std::string(noun) + " "}, RefInline{.stable_name = "x", .bare = true}};
        if (with_tie)
            std::get<TextInline>(para[0]).hints = {{4 + noun.size(), LayoutHintKind::Tie}};
        std::get<BodyParagraph>(reset.descr.elements[0].body[0]).text = std::move(para);
        return d;
    };
    const auto para_of = [](const Document& d) {
        return std::get<BodyParagraph>(
                   std::get<SpecItem>(std::get<Section>(d.nodes[0]).children[1]).descr.elements[0].body[0])
            .text;
    };
    const Document before = tied(true, "subclause");
    const Document after  = tied(false, "subclause");
    CHECK(!diff::has_marks(diff::compare(before, after, {.context = diff::Context::All})));
    CHECK(diff::compare(after, before, {.context = diff::Context::All}) == before);

    // The word before the tie is replaced. The mark is on the word, and the
    // tied space after it is outside the mark with its tie.
    const Paragraph para = para_of(diff::compare(tied(false, "subclause"), tied(true, "clause")));
    REQUIRE(para.size() == 5);
    CHECK(std::get<TextInline>(para[0]).text == "See ");
    CHECK(std::get<ChangedInline>(para[1]).inlines == std::vector<PlainInline>{TextInline{"subclause"}});
    CHECK(std::get<ChangedInline>(para[2]).inlines == std::vector<PlainInline>{TextInline{"clause"}});
    CHECK(std::get<TextInline>(para[3]) == TextInline{" ", {{0, LayoutHintKind::Tie}}});
    CHECK(std::holds_alternative<RefInline>(para[4]));
}

TEST_CASE("diff - a code mark spans the changed tokens, not the whitespace after them") {
    // Issue #258: a keyword added before a declaration is marked
    // `@\added{constexpr}@ R`, not `@\added{constexpr }@R`.
    Document before               = gadget();
    Document after                = gadget();
    auto&    ready                = std::get<SpecItem>(std::get<Section>(after.nodes[1]).children[0]);
    ready.decl.signatures[0].text = "constexpr static bool ready() const;";
    const auto marked             = [](const CodeText& code) {
        std::vector<std::string> out;
        for (const ChangeRun& run : code.changes)
            out.push_back(code.text.substr(run.begin, run.end - run.begin));
        return out;
    };
    {
        const Document out = diff::compare(before, after);
        const auto&    sig = std::get<SpecItem>(std::get<Section>(out.nodes[0]).children[0]).decl.signatures[0];
        CHECK(sig.text == "constexpr static bool ready() const;");
        CHECK(marked(sig) == std::vector<std::string>{"constexpr static"});
        CHECK(sig.changes[0].change == Change::Added);
    }
    {
        const Document out = diff::compare(after, before);
        const auto&    sig = std::get<SpecItem>(std::get<Section>(out.nodes[0]).children[0]).decl.signatures[0];
        CHECK(marked(sig) == std::vector<std::string>{"constexpr static"});
        CHECK(sig.changes[0].change == Change::Removed);
    }
    // A replaced token: the two marks abut, and the addition's spacing
    // separates the pair from what follows.
    Document consteval_after = gadget();
    std::get<SpecItem>(std::get<Section>(consteval_after.nodes[1]).children[0]).decl.signatures[0].text =
        "consteval bool ready() const;";
    Document constexpr_before = gadget();
    std::get<SpecItem>(std::get<Section>(constexpr_before.nodes[1]).children[0]).decl.signatures[0].text =
        "constexpr bool ready() const;";
    const Document out = diff::compare(constexpr_before, consteval_after);
    const auto&    sig = std::get<SpecItem>(std::get<Section>(out.nodes[0]).children[0]).decl.signatures[0];
    CHECK(sig.text == "constexprconsteval bool ready() const;");
    CHECK(marked(sig) == std::vector<std::string>{"constexpr", "consteval"});
}

// Issue #409: a `>` the baseline has one more of is removed against the
// brackets it closed, in the baseline's spacing, and the name after it keeps
// its one space.
TEST_CASE("diff - a removed closing bracket stays against the brackets it closed") {
    Document before = gadget();
    Document after  = gadget();
    std::get<SpecItem>(std::get<Section>(before.nodes[1]).children[0]).decl.signatures[0].text =
        "optional<remove_cv_t<invoke_result_t<F, T&>>> transform(F&& f) const;";
    std::get<SpecItem>(std::get<Section>(after.nodes[1]).children[0]).decl.signatures[0].text =
        "optional<invoke_result_t<F, T&>> transform(F&& f) const;";
    const Document out = diff::compare(before, after);
    const auto&    sig = std::get<SpecItem>(std::get<Section>(out.nodes[0]).children[0]).decl.signatures[0];
    CHECK(sig.text == "optional<remove_cv_t<invoke_result_t<F, T&>>> transform(F&& f) const;");
    REQUIRE(sig.changes.size() == 2);
    CHECK(sig.text.substr(sig.changes[0].begin, sig.changes[0].end - sig.changes[0].begin) == "remove_cv_t<");
    CHECK(sig.changes[1].begin == sig.text.find(">>> ") + 2);
    CHECK(sig.changes[1].end == sig.changes[1].begin + 1);
}

TEST_CASE("diff - a prose mark spans the changed words, not the whitespace around them") {
    Document before = gadget();
    Document after  = gadget();
    auto&    reset  = std::get<SpecItem>(std::get<Section>(after.nodes[1]).children[1]);
    std::get<BodyParagraph>(reset.descr.elements[0].body[0]).text = text("Resets the whole gadget.");
    const auto para_of                                            = [](const Document& d) {
        return std::get<BodyParagraph>(
                   std::get<SpecItem>(std::get<Section>(d.nodes[0]).children[1]).descr.elements[0].body[0])
            .text;
    };
    {
        const Paragraph para = para_of(diff::compare(before, after));
        REQUIRE(para.size() == 3);
        CHECK(std::get<TextInline>(para[0]).text == "Resets the ");
        const auto& added = std::get<ChangedInline>(para[1]);
        CHECK(added.change == Change::Added);
        CHECK(added.inlines == std::vector<PlainInline>{TextInline{"whole"}});
        CHECK(std::get<TextInline>(para[2]).text == " gadget.");
    }
    {
        const Paragraph para = para_of(diff::compare(after, before));
        REQUIRE(para.size() == 3);
        CHECK(std::get<ChangedInline>(para[1]).change == Change::Removed);
        CHECK(std::get<ChangedInline>(para[1]).inlines == std::vector<PlainInline>{TextInline{"whole"}});
        CHECK(std::get<TextInline>(para[2]).text == " gadget.");
    }
    // A replaced word: removal and addition abut.
    Document replaced = gadget();
    std::get<BodyParagraph>(
        std::get<SpecItem>(std::get<Section>(replaced.nodes[1]).children[1]).descr.elements[0].body[0])
        .text                                                     = text("Resets the widget fully.");
    std::get<BodyParagraph>(reset.descr.elements[0].body[0]).text = text("Resets the gizmo fully.");
    const Paragraph para                                          = para_of(diff::compare(replaced, after));
    REQUIRE(para.size() == 4);
    CHECK(std::get<TextInline>(para[0]).text == "Resets the ");
    CHECK(std::get<ChangedInline>(para[1]).inlines == std::vector<PlainInline>{TextInline{"widget"}});
    CHECK(std::get<ChangedInline>(para[2]).inlines == std::vector<PlainInline>{TextInline{"gizmo"}});
    CHECK(std::get<TextInline>(para[3]).text == " fully.");
}

TEST_CASE("diff - the items context renders one ellipsis where elisions meet across a section end") {
    // Issue #259: a changed subsection ending in elided children, followed by
    // an unchanged sibling subsection, is one ellipsis, not two: the
    // sibling's heading (issue #263) stands between them.
    const auto doc = [](std::string first_line) {
        Document d;
        d.nodes.push_back(Section{
            "top",
            "Top",
            {Section{
                 "top.a", "A", {FreeParagraph{text(std::move(first_line))}, FreeParagraph{text("Unchanged in a.")}}},
             Section{"top.b", "B", {FreeParagraph{text("Unchanged b.")}}},
             Section{"top.c", "C", {FreeParagraph{text("Also unchanged.")}}}}});
        return d;
    };
    const Document out = diff::compare(
        doc("The first line, before."), doc("The first line, after."), {.context = diff::Context::Items});
    REQUIRE(out.nodes.size() == 1);
    const auto& top = std::get<Section>(out.nodes[0]);
    REQUIRE(top.children.size() == 3);
    const auto& a = std::get<Section>(top.children[0]);
    REQUIRE(a.children.size() == 2);
    CHECK(std::get<FreeParagraph>(a.children[0]).change == Change::Same);
    CHECK(std::get<FreeParagraph>(a.children[1]).change == Change::Elided);
    const auto& b = std::get<Section>(top.children[1]);
    CHECK(b.stable_name == "top.b");
    REQUIRE(b.children.size() == 1);
    CHECK(std::get<FreeParagraph>(b.children[0]).change == Change::Elided);
    const auto& c = std::get<Section>(top.children[2]);
    CHECK(c.stable_name == "top.c");
    REQUIRE(c.children.size() == 1);
    CHECK(std::get<FreeParagraph>(c.children[0]).change == Change::Elided);
}

TEST_CASE("diff - the items context keeps an unchanged subsection's heading over one ellipsis") {
    // Issue #263: a synopsis that refers to an unchanged subclause by stable
    // name still finds its heading. So does a reference to one of that
    // subclause's own subsections, and the next sibling keeps its heading,
    // so every subclause numbers as in the draft.
    const auto doc = [](std::string synopsis) {
        Document d;
        d.nodes.push_back(
            Section{"top",
                    "Top",
                    {Synopsis{.name = "top", .code = {std::move(synopsis), {}}, .roster = {}},
                     FreeParagraph{text("Unchanged after the synopsis.")},
                     Section{"top.ctor",
                             "Constructors",
                             {FreeParagraph{text("Unchanged ctor.")},
                              Section{"top.ctor.more", "More", {FreeParagraph{text("Nested and unchanged.")}}}}},
                     Section{"top.inv", "Invocation", {FreeParagraph{text("Unchanged inv.")}}}}});
        return d;
    };
    const Document out = diff::compare(doc("// \\ref{top.ctor}, constructors\nint old_name;"),
                                       doc("// \\ref{top.ctor}, constructors\nint new_name;"),
                                       {.context = diff::Context::Items});
    REQUIRE(out.nodes.size() == 1);
    const auto& top = std::get<Section>(out.nodes[0]);
    REQUIRE(top.children.size() == 4);
    CHECK_FALSE(std::get<Synopsis>(top.children[0]).code.changes.empty());
    CHECK(std::get<FreeParagraph>(top.children[1]).change == Change::Elided);
    const auto& ctor = std::get<Section>(top.children[2]);
    CHECK(ctor.stable_name == "top.ctor");
    CHECK(ctor.title == "Constructors");
    CHECK(ctor.change == Change::Same);
    REQUIRE(ctor.children.size() == 2);
    CHECK(std::get<FreeParagraph>(ctor.children[0]).change == Change::Elided);
    const auto& more = std::get<Section>(ctor.children[1]);
    CHECK(more.stable_name == "top.ctor.more");
    REQUIRE(more.children.size() == 1);
    CHECK(std::get<FreeParagraph>(more.children[0]).change == Change::Elided);
    const auto& inv = std::get<Section>(top.children[3]);
    CHECK(inv.stable_name == "top.inv");
    REQUIRE(inv.children.size() == 1);
    CHECK(std::get<FreeParagraph>(inv.children[0]).change == Change::Elided);
}

TEST_CASE("diff - restoration law: stripping either side of the marks gives back that document") {
    Document before                                               = gadget();
    Document after                                                = gadget();
    auto&    observe                                              = std::get<Section>(after.nodes[1]);
    auto&    ready                                                = std::get<SpecItem>(observe.children[0]);
    ready.decl.signatures[0].text                                 = "bool ready() const noexcept;";
    std::get<BodyParagraph>(ready.descr.elements[0].body[0]).text = {CodeInline{{"true", {}}},
                                                                     TextInline{" if the gadget is armed."}};
    ready.descr.elements.push_back(element(ElementKind::Remarks, {BodyParagraph{text("New remark.")}}));
    std::get<SpecItem>(observe.children[1]).decl.entities           = {"clear"};
    std::get<SpecItem>(observe.children[1]).decl.signatures[0].text = "void clear();";
    std::get<Synopsis>(after.nodes[0]).code.text = "class gadget {\n  bool ready() const noexcept;\n};";
    std::get<BodyParagraph>(std::get<SpecItem>(observe.children[1]).descr.elements[0].body[0]).text =
        text("Resets the whole gadget.");

    const Document out = diff::compare(before, after, {.context = diff::Context::All});
    const Document restored_after{strip_nodes(out.nodes, Change::Added)};
    const Document restored_before{strip_nodes(out.nodes, Change::Removed)};
    CHECK(code_texts(restored_after) == code_texts(after));
    CHECK(code_texts(restored_before) == code_texts(before));
    // Prose, element lists and node marks restore exactly up to runs of
    // whitespace: a mark spans words and not the space around them, so a
    // word added or removed between two others leaves the side without it
    // two separators where it had one.
    const auto squeezed = [](Paragraph p) {
        for (Inline& piece : p)
            if (auto* t = std::get_if<TextInline>(&piece)) {
                auto [first, last] = std::ranges::unique(t->text, [](char x, char y) { return x == ' ' && y == ' '; });
                t->text.erase(first, last);
            }
        return p;
    };
    const auto prose_of = [&](const Document& d) {
        std::vector<Paragraph> out;
        for (const Node& n : d.nodes)
            if (const auto* sec = std::get_if<Section>(&n))
                for (const Node& child : sec->children)
                    if (const auto* it = std::get_if<SpecItem>(&child))
                        for (const DescriptionElement& e : it->descr.elements)
                            for (const BodyBlock& b : e.body)
                                if (const auto* p = std::get_if<BodyParagraph>(&b))
                                    out.push_back(squeezed(p->text));
        return out;
    };
    CHECK(prose_of(restored_after) == prose_of(after));
    CHECK(prose_of(restored_before) == prose_of(before));
}

TEST_CASE("diff - decls elides only unchanged nonempty descriptions") {
    const Document before         = gadget();
    Document       after          = before;
    auto&          section        = std::get<Section>(after.nodes[1]);
    auto&          ready          = std::get<SpecItem>(section.children[0]);
    ready.decl.signatures[0].text = "constexpr bool ready() const;";
    ready.descr.leading           = {BodyParagraph{text("Unchanged introductory text.")}};
    Document baseline             = before;
    std::get<SpecItem>(std::get<Section>(baseline.nodes[1]).children[0]).descr.leading = ready.descr.leading;
    const auto  result   = diff::compare(baseline, after, {.context = diff::Context::Decls});
    const auto& retained = std::get<SpecItem>(std::get<Section>(result.nodes[0]).children[0]);
    CHECK(!retained.decl.signatures[0].changes.empty());
    CHECK(retained.descr.elements.empty());
    REQUIRE(retained.descr.leading.size() == 1);
    CHECK(std::get<BodyParagraph>(retained.descr.leading[0]).change == Change::Elided);
    const auto full = diff::compare(baseline, after, {.context = diff::Context::Items});
    CHECK(std::get<SpecItem>(std::get<Section>(full.nodes[0]).children[0]).descr == ready.descr);

    SECTION("changed description remains whole") {
        ready.descr.leading = {BodyParagraph{text("Changed introductory text.")}};
        const auto  changed = diff::compare(baseline, after, {.context = diff::Context::Decls});
        const auto& descr   = std::get<SpecItem>(std::get<Section>(changed.nodes[0]).children[0]).descr;
        CHECK(!descr.elements.empty());
        CHECK(std::get<BodyParagraph>(descr.leading[0]).change != Change::Elided);
    }
    SECTION("an item without a description gains none") {
        ready.descr                                                                = {};
        std::get<SpecItem>(std::get<Section>(baseline.nodes[1]).children[0]).descr = {};
        const auto empty = diff::compare(baseline, after, {.context = diff::Context::Decls});
        CHECK(!has_description(std::get<SpecItem>(std::get<Section>(empty.nodes[0]).children[0]).descr));
    }
    SECTION("top-level items are elided too") {
        baseline.nodes = {std::get<SpecItem>(std::get<Section>(baseline.nodes[1]).children[0])};
        after.nodes    = {ready};
        const auto top = diff::compare(baseline, after, {.context = diff::Context::Decls});
        CHECK(std::get<SpecItem>(top.nodes[0]).descr.elements.empty());
    }
}

TEST_CASE("diff - decls preserves whole added and removed sections and items") {
    const Document doc   = gadget();
    const Document added = diff::compare({}, doc, {.context = diff::Context::Decls});
    CHECK(std::get<Section>(added.nodes[1]).children == std::get<Section>(doc.nodes[1]).children);
    const Document removed = diff::compare(doc, {}, {.context = diff::Context::Decls});
    CHECK(std::get<Section>(removed.nodes[1]).children == std::get<Section>(doc.nodes[1]).children);
    Document after    = doc;
    auto&    children = std::get<Section>(after.nodes[1]).children;
    children.erase(children.begin());
    children.push_back(item(
        "void start();", {"start"}, {element(ElementKind::Effects, {BodyParagraph{text("Starts the gadget.")}})}));
    const auto  out     = diff::compare(doc, after, {.context = diff::Context::Decls});
    const auto& changed = std::get<Section>(out.nodes[0]);
    CHECK(std::get<SpecItem>(changed.children.front()).change == Change::Removed);
    CHECK(has_description(std::get<SpecItem>(changed.children.front()).descr));
    CHECK(std::get<SpecItem>(changed.children.back()).change == Change::Added);
    CHECK(has_description(std::get<SpecItem>(changed.children.back()).descr));
}

TEST_CASE("diff - a block that opens a numbered paragraph in one revision and not the other is a change") {
    const auto with_note = [](bool opens) {
        Document doc;
        doc.nodes.push_back(
            Section{"s",
                    "S",
                    {item("void f();",
                          {"f"},
                          {element(ElementKind::Returns,
                                   {BodyParagraph{text("A reference.")},
                                    Note{{BodyParagraph{text("About it.")}}, Change::Same, opens}})})}});
        return doc;
    };
    const Document drafted  = with_note(true);
    const Document authored = with_note(false);
    const auto     body_of  = [](const Document& doc) {
        return std::get<SpecItem>(std::get<Section>(doc.nodes.front()).children.front()).descr.elements.front().body;
    };

    // The same flag is no change; a different one is shown, even in the
    // default context, which keeps only what changed (issue #365).
    CHECK(diff::compare(drafted, drafted).nodes.empty());
    REQUIRE_FALSE(diff::compare(drafted, authored).nodes.empty());
    REQUIRE_FALSE(diff::compare(authored, drafted).nodes.empty());

    // The note is removed as the baseline numbered it and added as the
    // current revision does; the prose before it is untouched.
    const DescriptionBody forward = body_of(diff::compare(drafted, authored, {.context = diff::Context::All}));
    REQUIRE(forward.size() == 3);
    CHECK(block_change(forward[0]) == Change::Same);
    CHECK(std::get<Note>(forward[1]).change == Change::Removed);
    CHECK(std::get<Note>(forward[1]).opens_paragraph);
    CHECK(std::get<Note>(forward[2]).change == Change::Added);
    CHECK_FALSE(std::get<Note>(forward[2]).opens_paragraph);

    // A paragraph that continues the one before it in one revision and opens
    // its own in the other is the same case for prose.
    const auto with_prose = [](bool continuation) {
        Document doc;
        doc.nodes.push_back(Section{"s",
                                    "S",
                                    {item("void f();",
                                          {"f"},
                                          {element(ElementKind::Returns,
                                                   {BodyParagraph{text("A reference.")},
                                                    Itemize{{text("one"), text("two")}},
                                                    BodyParagraph{text("Then more."), continuation}})})}});
        return doc;
    };
    const DescriptionBody prose =
        body_of(diff::compare(with_prose(true), with_prose(false), {.context = diff::Context::All}));
    REQUIRE(prose.size() == 4);
    CHECK(block_change(prose[2]) == Change::Removed);
    CHECK(block_change(prose[3]) == Change::Added);
}

TEST_CASE("diff - a list item's blocks diff as a body does, and an unpaired item takes its blocks with it") {
    // Issue #367.
    const auto with_items = [](std::vector<ListItem> items) {
        Document doc;
        doc.nodes.push_back(Section{
            "s", "S", {item("void f();", {"f"}, {element(ElementKind::Effects, {Itemize{std::move(items)}})})}});
        return doc;
    };
    const auto code_item = [](std::string lead, std::string code) {
        return ListItem{text(std::move(lead)), DescriptionBody{AuthoredCode{CodeText{std::move(code), {}}}}};
    };
    const auto list_of = [](const Document& doc) {
        return std::get<Itemize>(std::get<SpecItem>(std::get<Section>(doc.nodes.front()).children.front())
                                     .descr.elements.front()
                                     .body.front());
    };

    const Document before = with_items({code_item("If a, equivalent to:", "f(x);"), text("Otherwise, no effect.")});
    const Document after  = with_items({code_item("If a, equivalent to:", "f(y);"), text("Otherwise, no effect.")});
    const Itemize  edited = list_of(diff::compare(before, after, {.context = diff::Context::All}));
    REQUIRE(edited.items.size() == 2);
    const auto& code = std::get<AuthoredCode>(edited.items[0].blocks.at(0));
    CHECK(code.change == Change::Same);
    CHECK_FALSE(code.code.changes.empty());

    const Document removed = with_items({text("Otherwise, no effect.")});
    const Itemize  gone    = list_of(diff::compare(before, removed, {.context = diff::Context::All}));
    REQUIRE(gone.items.size() == 2);
    CHECK(std::get<AuthoredCode>(gone.items[0].blocks.at(0)).change == Change::Removed);
}
