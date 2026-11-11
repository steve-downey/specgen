// tests/beman/specgen/diff.test.cpp                                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/diff.hpp>
#include <beman/specgen/diff.hpp> // Re-inclusion verification

#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>
#include <string>
#include <variant>
#include <vector>

using namespace beman::specgen::ir;
namespace diff = beman::specgen::diff;

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
    for (const diff::Context c : {diff::Context::Changed, diff::Context::All, diff::Context::Items}) {
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
    // an unchanged sibling subsection, is one ellipsis, not two.
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
    REQUIRE(top.children.size() == 1);
    const auto& a = std::get<Section>(top.children[0]);
    REQUIRE(a.children.size() == 2);
    CHECK(std::get<FreeParagraph>(a.children[0]).change == Change::Same);
    CHECK(std::get<FreeParagraph>(a.children[1]).change == Change::Elided);
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
