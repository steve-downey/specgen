// tests/beman/specgen/backend/org.test.cpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Like `mpark.test.cpp`, these shadow `latex.test.cpp` case for case:
// the same hand-built IR, asserted against the third target's conventions.
// Where a case here asserts the *same* text its LaTeX twin does, that is not
// duplication -- it is this backend's central claim, that the bytes inside an
// org code block and inside the draft's own environment are the same bytes,
// because org exports the one to the other.

#include <beman/specgen/backend/org.hpp>
#include <beman/specgen/backend/org.hpp> // Re-inclusion verification

#include <beman/specgen/backend/common.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace org    = beman::specgen::backend::org;
namespace common = beman::specgen::backend::common;
using namespace beman::specgen::ir;

TEST_CASE("org - HeaderIsIdempotent") {
    // Placeholder: verifies header re-inclusion safety and build coherency.
    // This test always passes if the file compiles.
    REQUIRE(true);
}

// The same [optional.observe]/value_or shape both sibling files open with, so
// the three expected strings can be read side by side.
TEST_CASE("org - value_or golden") {
    SpecItem item;
    // Dropped, per design §8 — asserted below by the absence of any index text.
    item.decl.index.push_back({IndexKind::Member, "value_or", "optional"});
    item.decl.signatures.push_back(
        {"template<class U = remove_cv_t<T>> constexpr remove_cv_t<T> value_or(U&& v) const &;", {}});

    DescriptionElement mandates;
    mandates.kind = ElementKind::Mandates;
    mandates.paragraphs.push_back({CodeInline{{"is_copy_constructible_v<T>", {}}},
                                   TextInline{" is "},
                                   CodeInline{{"true", {}}},
                                   TextInline{" and "},
                                   CodeInline{{"is_convertible_v<U, T>", {}}},
                                   TextInline{" is "},
                                   CodeInline{{"true", {}}},
                                   TextInline{"."}});
    item.descr.elements.push_back(std::move(mandates));

    DescriptionElement effects;
    effects.kind = ElementKind::Effects;
    effects.equivalent =
        EquivalentTo{{"return has_value() ? **this : static_cast<remove_cv_t<T>>(std::forward<U>(v));", {}}};
    item.descr.elements.push_back(std::move(effects));

    const std::string expected =
        R"(#+begin_itemdecl
template<class U = remove_cv_t<T>> constexpr remove_cv_t<T> value_or(U&& v) const &;
#+end_itemdecl

/Mandates/: ~is_copy_constructible_v<T>~ is ~true~ and ~is_convertible_v<U, T>~ is ~true~.

/Effects/: Equivalent to:

#+begin_codeblock
return has_value() ? **this : static_cast<remove_cv_t<T>>(std::forward<U>(v));
#+end_codeblock
)";

    CHECK(org::render_to_string(item) == expected);
}

// --- the shared-convention claim -------------------------------------------

// The reason `draft_span_*` lives in backend/common.hpp. An
// org code block is exported to the draft's `\begin{itemdecl}`, where
// `escapechar=@` is in force, so the escape is not "org's" at all.
TEST_CASE("org - a span in a code block uses the draft's own @...@ escape") {
    SpecItem item;
    // "VAL" is the sentinel occupying the exposition-only name's byte range.
    item.decl.signatures.push_back({"T VAL;", {{2, 5, SpanKind::ExposId, "val"}}});

    const std::string out = org::render_to_string(item);
    CHECK(out.find("T @\\exposidnc{val}@;") != std::string::npos);
    // And the escape is literally the shared function's output, not a
    // lookalike -- if latex.cpp's convention ever changes, this moves with it.
    CHECK(out.find(common::draft_span_codeblock({2, 5, SpanKind::ExposId, "val"}, "VAL")) != std::string::npos);
}

TEST_CASE("org - a see-below span in a code block") {
    SpecItem item;
    item.decl.signatures.push_back({"SEEBELOW f();", {{0, 8, SpanKind::SeeBelow, ""}}});

    CHECK(org::render_to_string(item).find("@\\seebelow@ f();") != std::string::npos);
}

TEST_CASE("org - an implementation-defined span in a code block") {
    SpecItem item;
    item.decl.signatures.push_back(
        {"using iterator = implementation-defined;", {{17, 39, SpanKind::ImplDefined, ""}}});

    CHECK(org::render_to_string(item).find("using iterator = @\\impdef@;") != std::string::npos);
}

TEST_CASE("org - a placeholder span in a code block") {
    SpecItem item;
    item.decl.signatures.push_back({"void f(PH);", {}});
    item.decl.signatures.back().spans.push_back({7, 9, SpanKind::Placeholder, "unspecified"});

    CHECK(org::render_to_string(item).find("void f(@\\placeholder{unspecified}@);") != std::string::npos);
}

// The LaTeX backend's Ref exception, inherited rather than re-decided:
// `texcl=true` is set in
// the shared `\lstset`, so it holds inside `codeblock` and `itemdecl` alike and
// a `//` comment in either is already in TeX mode. This is the one span kind
// the draft writes bare -- and where the mpark backend wraps unconditionally,
// this backend must not, because it really is emitting into that environment.
TEST_CASE("org - a ref span in a synopsis group comment is bare, not @-escaped") {
    Document doc;
    doc.nodes.push_back(Synopsis{
        .name   = "optional",
        .code   = {"class optional {\n  // [optional.ctor], constructors\n  optional();\n};",
                   {{22, 37, SpanKind::Ref, "optional.ctor"}}},
        .roster = {},
    });

    const std::string out = org::render_to_string(doc);
    CHECK(out.find("// \\ref{optional.ctor}, constructors") != std::string::npos);
    CHECK(out.find("@\\ref{") == std::string::npos);
    // The parenthesized form belongs to RefInline, in prose, and must not
    // leak into a comment.
    CHECK(out.find("\\iref{") == std::string::npos);
}

// --- prose: org, except where org cannot say it ----------------------------

TEST_CASE("org - a span-free code inline is org's own code markup") {
    SpecItem           item;
    DescriptionElement remarks;
    remarks.kind = ElementKind::Remarks;
    remarks.paragraphs.push_back({TextInline{"Returns "}, CodeInline{{"has_value()", {}}}, TextInline{"."}});
    item.descr.elements.push_back(std::move(remarks));

    const std::string out = org::render_to_string(item);
    CHECK(out.find("Returns ~has_value()~.") != std::string::npos);
    // No LaTeX at all for the ordinary case -- that is what makes this an org
    // backend rather than a LaTeX one wearing org headings.
    CHECK(out.find("@@latex:") == std::string::npos);
}

// `@...@` is a *listings* option and is inert out in prose, so a span there
// cannot use the code-block spelling. It leaves org instead. The payload is
// the draft's own prose rendering, shared with the LaTeX backend.
TEST_CASE("org - a whole-span code inline becomes a latex export snippet") {
    SpecItem           item;
    DescriptionElement remarks;
    remarks.kind = ElementKind::Remarks;
    remarks.paragraphs.push_back({CodeInline{{"VAL", {{0, 3, SpanKind::ExposId, "val"}}}}});
    item.descr.elements.push_back(std::move(remarks));

    const std::string out = org::render_to_string(item);
    // Bare `\exposid`, not `\tcode{\exposid{...}}`: the macro already sets the
    // code font. This is the whole-span rule the LaTeX backend applies, and it
    // applies here because the snippet's contents *are* draft LaTeX -- the
    // opposite of the mpark backend, whose twin case asserts no such shortcut.
    CHECK(out.find("@@latex:\\exposid{val}@@") != std::string::npos);
    CHECK(out.find("\\tcode{") == std::string::npos);
}

TEST_CASE("org - a partially-spanned code inline keeps its tcode wrapper") {
    SpecItem           item;
    DescriptionElement remarks;
    remarks.kind = ElementKind::Remarks;
    remarks.paragraphs.push_back(
        {TextInline{"Equivalent to "}, CodeInline{{"x.VAL", {{2, 5, SpanKind::ExposId, "val"}}}}});
    item.descr.elements.push_back(std::move(remarks));

    CHECK(org::render_to_string(item).find("Equivalent to @@latex:\\tcode{x.\\exposid{val}}@@") != std::string::npos);
}

// Org has no escape for a literal `~` inside `~...~`, and C++ spells two real
// things with it. Such an inline takes the export-snippet route the spanned
// ones take, which is why this costs no extra branch.
TEST_CASE("org - a code inline containing a tilde falls back to a snippet") {
    SpecItem           item;
    DescriptionElement effects;
    effects.kind = ElementKind::Effects;
    effects.paragraphs.push_back({TextInline{"Calls "}, CodeInline{{"~optional()", {}}}, TextInline{"."}});
    item.descr.elements.push_back(std::move(effects));

    const std::string out = org::render_to_string(item);
    CHECK(out.find("Calls @@latex:\\tcode{~optional()}@@.") != std::string::npos);
    // The naive rendering would have closed org's code run at the second
    // character and spilled the rest into prose.
    CHECK(out.find("~~optional()~") == std::string::npos);
}

// The prose cross-reference. `\iref` is the parenthesized form, so the parens
// belong to this rendering; plain text rather than an org link, per the
// checkpoint.
TEST_CASE("org - a RefInline in prose is parenthesized plain text") {
    SpecItem           item;
    DescriptionElement remarks;
    remarks.kind = ElementKind::Remarks;
    remarks.paragraphs.push_back({TextInline{"See "}, RefInline{"optional.general"}, TextInline{"."}});
    item.descr.elements.push_back(std::move(remarks));

    const std::string out = org::render_to_string(item);
    CHECK(out.find("See ([optional.general]).") != std::string::npos);
    CHECK(out.find("[[") == std::string::npos); // no org link syntax
}

TEST_CASE("org - a concept reference is code font") {
    SpecItem           item;
    DescriptionElement constraints;
    constraints.kind = ElementKind::Constraints;
    constraints.paragraphs.push_back(
        {CodeInline{{"T", {}}}, TextInline{" models "}, ConceptRef{"copy_constructible"}, TextInline{"."}});
    item.descr.elements.push_back(std::move(constraints));

    CHECK(org::render_to_string(item).find("~T~ models ~copy_constructible~.") != std::string::npos);
}

// --- elements ---------------------------------------------------------------

TEST_CASE("org - every element kind renders as its draft label, in italics") {
    for (int i = 0; i <= static_cast<int>(ElementKind::Errors); ++i) {
        const auto         kind = static_cast<ElementKind>(i);
        SpecItem           item;
        DescriptionElement element;
        element.kind = kind;
        element.paragraphs.push_back({TextInline{"x."}});
        item.descr.elements.push_back(std::move(element));

        const std::string label = '/' + std::string(common::element_label(kind)) + "/: x.";
        CHECK(org::render_to_string(item).find(label) != std::string::npos);
    }
}

// The three kinds where the draft's label is not the macro spelling -- the
// reason `element_label` exists rather than `ir::element_name` being reused.
TEST_CASE("org - the labels that differ from the macro name") {
    SpecItem           item;
    DescriptionElement expects;
    expects.kind = ElementKind::Expects;
    expects.paragraphs.push_back({TextInline{"x."}});
    item.descr.elements.push_back(std::move(expects));

    const std::string out = org::render_to_string(item);
    CHECK(out.find("/Preconditions/: x.") != std::string::npos);
    CHECK(out.find("/expects/") == std::string::npos);
}

// No paragraph numbers anywhere (checkpoint note 4), so a second paragraph is
// plain prose -- where both sibling backends emit a fresh `\pnum`/`[#]{.pnum}`.
TEST_CASE("org - a multi-paragraph element repeats neither label nor number") {
    SpecItem           item;
    DescriptionElement effects;
    effects.kind = ElementKind::Effects;
    effects.paragraphs.push_back({TextInline{"First."}});
    effects.paragraphs.push_back({TextInline{"Second."}});
    item.decl.signatures.push_back({"int f();", {}});
    item.descr.elements.push_back(std::move(effects));

    const std::string expected = R"(#+begin_itemdecl
int f();
#+end_itemdecl

/Effects/: First.

Second.
)";
    CHECK(org::render_to_string(item) == expected);
}

TEST_CASE("org - an itemize with no lead-in prose carries the label") {
    SpecItem           item;
    DescriptionElement constraints;
    constraints.kind    = ElementKind::Constraints;
    constraints.itemize = Itemize{{{TextInline{"one,"}}, {TextInline{"two."}}}};
    item.decl.signatures.push_back({"int f();", {}});
    item.descr.elements.push_back(std::move(constraints));

    const std::string expected = R"(#+begin_itemdecl
int f();
#+end_itemdecl

/Constraints/:

- one,
- two.
)";
    CHECK(org::render_to_string(item) == expected);
}

TEST_CASE("org - an itemize with lead-in prose stays in that paragraph") {
    SpecItem           item;
    DescriptionElement constraints;
    constraints.kind = ElementKind::Constraints;
    constraints.paragraphs.push_back({TextInline{"All of:"}});
    constraints.itemize = Itemize{{{TextInline{"one."}}}};
    item.descr.elements.push_back(std::move(constraints));

    CHECK(org::render_to_string(item).find("/Constraints/: All of:\n\n- one.\n") != std::string::npos);
}

TEST_CASE("org - same-kind authored itemizes concatenate") {
    SpecItem           item;
    DescriptionElement first;
    first.kind    = ElementKind::Constraints;
    first.itemize = Itemize{{{TextInline{"one,"}}}};
    DescriptionElement second;
    second.kind         = ElementKind::Constraints;
    second.itemize      = Itemize{{{TextInline{"two."}}}};
    item.descr.elements = {std::move(first), std::move(second)};

    const std::string out = org::render_to_string(item);
    CHECK(out.find("- one,\n- two.\n") != std::string::npos);
}

TEST_CASE("org - an authored two-dimensional table is a named native table") {
    DescriptionElement effects;
    effects.kind  = ElementKind::Effects;
    effects.table = Table2D{
        .stable_name = "optional.assign.copy",
        .caption     = {CodeInline{{"operator=", {}}}, TextInline{" effects"}},
        .column1     = {TextInline{"has | value"}},
        .column2     = {TextInline{"has no value"}},
        .rows        = {Table2DRow{.header = {CodeInline{{"rhs", {}}}},
                                   .cell1  = {TextInline{"assigns"}},
                                   .cell2  = {TextInline{"initializes"}}},
                        Table2DRow{.header = {TextInline{"empty"}},
                                   .cell1  = {TextInline{"destroys"}},
                                   .cell2  = {TextInline{"no effect"}}}},
    };
    SpecItem item;
    item.decl.signatures.push_back({"void f();", {}});
    DescriptionElement second = effects;
    second.table->stable_name = "optional.assign.second";
    item.descr.elements.push_back(std::move(effects));
    item.descr.elements.push_back(std::move(second));

    const std::string out = org::render_to_string(item);
    CHECK(out.find("/Effects/:\n\n"
                   "#+name: optional.assign.copy\n"
                   "#+caption: ~operator=~ effects\n"
                   "| | has \\vert{} value | has no value |\n"
                   "|-\n"
                   "| ~rhs~ | assigns | initializes |\n"
                   "| empty | destroys | no effect |\n") != std::string::npos);
    const auto first_table  = out.find("#+name: optional.assign.copy\n");
    const auto second_table = out.find("#+name: optional.assign.second\n");
    REQUIRE(first_table != std::string::npos);
    REQUIRE(second_table != std::string::npos);
    CHECK(first_table < second_table);
}

// A derived element and its authored twin are one description, so the
// label is emitted once -- the third backend to implement that fold.
TEST_CASE("org - adjacent same-kind elements share one label") {
    SpecItem           item;
    DescriptionElement derived;
    derived.kind    = ElementKind::Mandates;
    derived.derived = true;
    derived.paragraphs.push_back({TextInline{"Derived."}});
    DescriptionElement authored;
    authored.kind = ElementKind::Mandates;
    authored.paragraphs.push_back({TextInline{"Authored."}});
    item.descr.elements.push_back(std::move(derived));
    item.descr.elements.push_back(std::move(authored));

    const std::string out = org::render_to_string(item);
    CHECK(out.find("/Mandates/: Derived.\n\nAuthored.\n") != std::string::npos);
    // Once, not twice.
    CHECK(out.find("/Mandates/", out.find("/Mandates/") + 1) == std::string::npos);
}

TEST_CASE("org - grouped overloads share one itemdecl block") {
    SpecItem item;
    item.decl.signatures.push_back({"int f();", {}});
    item.decl.signatures.push_back({"int f(int);", {}});

    CHECK(org::render_to_string(item) == "#+begin_itemdecl\nint f();\nint f(int);\n#+end_itemdecl\n");
}

TEST_CASE("org - a decl with no description emits only its itemdecl") {
    SpecItem item;
    item.decl.signatures.push_back({"int f();", {}});

    CHECK(org::render_to_string(item) == "#+begin_itemdecl\nint f();\n#+end_itemdecl\n");
}

TEST_CASE("org - a description with no declaration emits no itemdecl block") {
    // A class's own wording (design §6): an itemdescr with no itemdecl. An
    // empty itemdecl block would be the blank box design §9 rejects on a
    // synopsis.
    SpecItem item;
    item.descr.elements.push_back({ElementKind::Remarks, {{TextInline{"A defined class's own description."}}}, {}});

    const std::string out = org::render_to_string(item);
    CHECK(out.find("itemdecl") == std::string::npos);
    CHECK(out == "/Remarks/: A defined class's own description.\n");
}

TEST_CASE("org - index entries are dropped") {
    SpecItem item;
    item.decl.signatures.push_back(
        {"optional value;", {{0, 8, SpanKind::LibraryIndex, ""}, {9, 14, SpanKind::LibraryIndex, "optional"}}});
    item.decl.index.push_back({IndexKind::Global, "optional", ""});
    item.decl.index.push_back({IndexKind::Member, "value", "optional"});

    // Unlike the mpark backend this one *could* emit `\indexlibrary` and have
    // it work, since the draft's index machinery is loaded -- so this is a
    // policy about paper fragments, and worth pinning as one.
    const std::string out = org::render_to_string(item);
    CHECK(out.find("index") == std::string::npos);
    CHECK(out.find("optional value;") != std::string::npos);
}

// --- documents --------------------------------------------------------------

TEST_CASE("org - document with sections, synopsis, and free prose") {
    Document doc;
    Section  sec;
    sec.stable_name = "optional.observe";
    sec.title       = "Observers";
    sec.children.push_back(
        Synopsis{.name = {}, .code = {"constexpr bool has_value() const noexcept;", {}}, .roster = {}});
    sec.children.push_back(FreeParagraph{{TextInline{"A program that instantiates it is ill-formed."}}});

    Section nested;
    nested.stable_name = "optional.observe.deep";
    nested.title       = "Deeper";
    sec.children.push_back(std::move(nested));
    doc.nodes.push_back(std::move(sec));

    // A nested section descends one outline level. No wrapper around the root
    // (checkpoint note 4) and no numbering on the free paragraph.
    const std::string expected =
        R"(** Observers [optional.observe]

#+begin_codeblock
constexpr bool has_value() const noexcept;
#+end_codeblock

A program that instantiates it is ill-formed.

*** Deeper [optional.observe.deep]
)";
    CHECK(org::render_to_string(doc) == expected);
}

TEST_CASE("org - base heading level is an option") {
    Document doc;
    Section  sec;
    sec.stable_name = "optional.ctor";
    sec.title       = "Constructors";
    doc.nodes.push_back(std::move(sec));

    CHECK(org::render_to_string(doc, {.base_heading_level = 4}) == "**** Constructors [optional.ctor]\n");
}

// Unlike the mpark backend there is no cap: markdown stops at six heading
// levels and org does not, so a deep document keeps descending.
TEST_CASE("org - outline level is not capped at six") {
    Document doc;
    Section  sec;
    sec.stable_name = "deep";
    doc.nodes.push_back(std::move(sec));

    CHECK(org::render_to_string(doc, {.base_heading_level = 8}) == "******** [deep]\n");
}

TEST_CASE("org - a titleless section emits no double space") {
    Document doc;
    Section  sec;
    sec.stable_name = "optional.ctor";
    doc.nodes.push_back(std::move(sec));

    CHECK(org::render_to_string(doc) == "** [optional.ctor]\n");
}

// Design §8's "the including document owns framing", with nothing overriding
// it here: org's `wording` environment restyles section numbering rather than
// enabling anything a fragment needs, so -- unlike mpark's `::: wording`, which
// `[#]{.pnum}` forces -- nothing wraps the fragment.
TEST_CASE("org - a fragment carries no wording wrapper") {
    Document doc;
    SpecItem first;
    first.decl.signatures.push_back({"int f();", {}});
    SpecItem second;
    second.decl.signatures.push_back({"int g();", {}});
    doc.nodes.push_back(std::move(first));
    doc.nodes.push_back(std::move(second));

    const std::string out = org::render_to_string(doc);
    CHECK(out.find("#+begin_wording") == std::string::npos);
    CHECK(out == "#+begin_itemdecl\nint f();\n#+end_itemdecl\n\n#+begin_itemdecl\nint g();\n#+end_itemdecl\n");
}

TEST_CASE("org - a synopsis is a codeblock, an itemdecl is an itemdecl") {
    Document doc;
    doc.nodes.push_back(Synopsis{.name = "optional", .code = {"template<class T> class optional;", {}}, .roster = {}});

    CHECK(org::render_to_string(doc) == "#+begin_codeblock\ntemplate<class T> class optional;\n#+end_codeblock\n");
}
