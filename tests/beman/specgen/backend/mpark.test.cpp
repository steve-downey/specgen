// tests/beman/specgen/backend/mpark.test.cpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// These deliberately shadow `latex.test.cpp` case for case: the same
// hand-built IR, asserted against the other target's conventions. Where a case
// here has no twin there, or asserts the *opposite* of its twin, that is a
// real difference between the two backends and the comment says which.

#include <beman/specgen/backend/mpark.hpp>
#include <beman/specgen/backend/mpark.hpp> // Re-inclusion verification

#include <beman/specgen/backend/common.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace mpark  = beman::specgen::backend::mpark;
namespace common = beman::specgen::backend::common;
using namespace beman::specgen::ir;

TEST_CASE("mpark - HeaderIsIdempotent") {
    // Placeholder: verifies header re-inclusion safety and build coherency.
    // This test always passes if the file compiles.
    REQUIRE(true);
}

// The same [optional.observe]/value_or shape latex.test.cpp opens with, so the
// two expected strings can be read side by side.
TEST_CASE("mpark - value_or golden") {
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
        R"(::: wording

```cpp
template<class U = remove_cv_t<T>> constexpr remove_cv_t<T> value_or(U&& v) const &;
```

[#]{.pnum} *Mandates*: `is_copy_constructible_v<T>` is `true` and `is_convertible_v<U, T>` is `true`.

[#]{.pnum} *Effects*: Equivalent to:

```cpp
return has_value() ? **this : static_cast<remove_cv_t<T>>(std::forward<U>(v));
```

:::
)";

    CHECK(mpark::render_to_string(item) == expected);
}

TEST_CASE("mpark - spans use the framework's embedded-Markdown convention") {
    SpecItem item;
    // "VAL" is the sentinel occupying the exposition-only name's byte range.
    item.decl.signatures.push_back({"T VAL;", {{2, 5, SpanKind::ExposId, "val"}}});

    DescriptionElement remarks;
    remarks.kind = ElementKind::Remarks;
    remarks.paragraphs.push_back({TextInline{"The type is "},
                                  CodeInline{{"SB", {{0, 2, SpanKind::SeeBelow, ""}}}},
                                  TextInline{", see "},
                                  RefInline{"optional.general"},
                                  TextInline{"."}});
    item.descr.elements.push_back(std::move(remarks));

    const std::string out = mpark::render_to_string(item);
    // `$x$` is the framework's shorthand for `@*x*@`, and it works the same
    // inside a fence and inside inline code — which is why, unlike the LaTeX
    // backend, there is only one escape function and no `@...@`-in-codeblock
    // counterpart to assert.
    CHECK(out.find("T $val$;") != std::string::npos);
    CHECK(out.find("`$see below$`") != std::string::npos);
    CHECK(out.find("see ([optional.general]{- .sref}).") != std::string::npos);
}

TEST_CASE("mpark - implementation-defined spans use the framework's italic shorthand") {
    SpecItem item;
    item.decl.signatures.push_back(
        {"using iterator = implementation-defined;", {{17, 39, SpanKind::ImplDefined, ""}}});
    CHECK(mpark::render_to_string(item).find("using iterator = $implementation-defined$;") != std::string::npos);
}

// The opposite assertion to its latex.test.cpp twin, on purpose: there, a
// CodeInline that is entirely one span drops the \tcode wrapper because
// \exposid/\seebelow already set the code font. Here the backticks *are* the
// code font, so dropping them would lose it.
TEST_CASE("mpark - a whole-span code inline keeps its backticks") {
    SpecItem           item;
    DescriptionElement remarks;
    remarks.kind = ElementKind::Remarks;
    remarks.paragraphs.push_back({CodeInline{{"VAL", {{0, 3, SpanKind::ExposId, "val"}}}}});
    item.descr.elements.push_back(std::move(remarks));

    const std::string out = mpark::render_to_string(item);
    CHECK(out.find("`$val$`") != std::string::npos);
}

TEST_CASE("mpark - a partially-spanned code inline is one backticked run") {
    SpecItem           item;
    DescriptionElement remarks;
    remarks.kind = ElementKind::Remarks;
    remarks.paragraphs.push_back(
        {TextInline{"Equivalent to "}, CodeInline{{"x.VAL", {{2, 5, SpanKind::ExposId, "val"}}}}});
    item.descr.elements.push_back(std::move(remarks));

    const std::string out = mpark::render_to_string(item);
    CHECK(out.find("`x.$val$`") != std::string::npos);
}

TEST_CASE("mpark - a placeholder span") {
    SpecItem item;
    item.decl.signatures.push_back({"void f(PH);", {}});
    item.decl.signatures.back().spans.push_back({7, 9, SpanKind::Placeholder, "unspecified"});

    CHECK(mpark::render_to_string(item).find("void f($unspecified$);") != std::string::npos);
}

// A Ref span is a cross-reference in a synopsis group comment, where
// the draft's `\ref` sets a bare "[optional.ctor]" -- so no parentheses, unlike
// the RefInline case above, which is `\iref`'s parenthesized prose form. The
// display text in CodeText::text is already the rendered shape.
TEST_CASE("mpark - a ref span in a synopsis group comment") {
    Document doc;
    doc.nodes.push_back(Synopsis{
        .name   = "optional",
        .code   = {"class optional {\n  // [optional.ctor], constructors\n  optional();\n};",
                   {{22, 37, SpanKind::Ref, "optional.ctor"}}},
        .roster = {},
    });

    const std::string out = mpark::render_to_string(doc);
    CHECK(out.find("// @[optional.ctor]{- .sref}@, constructors") != std::string::npos);
    // The parenthesized form belongs to RefInline, in prose, and must not
    // leak into a comment.
    CHECK(out.find("([optional.ctor]") == std::string::npos);
    // And no LaTeX survives into markdown -- the whole point of this backend.
    CHECK(out.find("\\ref{") == std::string::npos);
}

TEST_CASE("mpark - a concept reference is code font") {
    SpecItem           item;
    DescriptionElement constraints;
    constraints.kind = ElementKind::Constraints;
    constraints.paragraphs.push_back(
        {CodeInline{{"T", {}}}, TextInline{" models "}, ConceptRef{"copy_constructible"}, TextInline{"."}});
    item.descr.elements.push_back(std::move(constraints));

    CHECK(mpark::render_to_string(item).find("`T` models `copy_constructible`.") != std::string::npos);
}

TEST_CASE("mpark - every element kind renders as its draft label") {
    for (int i = 0; i <= static_cast<int>(ElementKind::Errors); ++i) {
        const auto kind = static_cast<ElementKind>(i);
        SpecItem   item;
        item.decl.signatures.push_back({"int f();", {}});
        item.descr.elements.push_back({kind, {{TextInline{"Prose."}}}, {}});

        const std::string out      = mpark::render_to_string(item);
        const std::string expected = "[#]{.pnum} *" + std::string(common::element_label(kind)) + "*: Prose.";
        INFO("kind: " << element_name(kind));
        CHECK(out.find(expected) != std::string::npos);
    }
}

// The labels are [structure.specifications]'s, not ir::element_name's macro
// spellings: three of the twelve differ, and those three are the ones a
// hand-written table gets wrong.
TEST_CASE("mpark - the labels that differ from the macro name") {
    CHECK(common::element_label(ElementKind::Expects) == "Preconditions");
    CHECK(common::element_label(ElementKind::HardExpects) == "Hardened preconditions");
    CHECK(common::element_label(ElementKind::Ensures) == "Postconditions");
    CHECK(common::element_label(ElementKind::Sync) == "Synchronization");
    CHECK(common::element_label(ElementKind::Errors) == "Error conditions");
}

TEST_CASE("mpark - multi-paragraph element repeats the pnum but not the label") {
    SpecItem item;
    item.decl.signatures.push_back({"int f();", {}});
    item.descr.elements.push_back({ElementKind::Effects, {{TextInline{"First."}}, {TextInline{"Second."}}}, {}});

    const std::string expected =
        R"(::: wording

```cpp
int f();
```

[#]{.pnum} *Effects*: First.

[#]{.pnum} Second.

:::
)";
    CHECK(mpark::render_to_string(item) == expected);
}

TEST_CASE("mpark - an itemize is a nested auto-numbered sublist") {
    SpecItem item;
    item.decl.signatures.push_back({"int f();", {}});
    DescriptionElement constraints;
    constraints.kind = ElementKind::Constraints;
    constraints.itemize =
        Itemize{{{CodeInline{{"A", {}}}, TextInline{" is "}, CodeInline{{"true", {}}}, TextInline{","}},
                 {CodeInline{{"B", {}}}, TextInline{" is "}, CodeInline{{"false", {}}}, TextInline{"."}}}};
    item.descr.elements.push_back(std::move(constraints));

    // No lead-in prose, so the list is the element's whole content and opens
    // its own numbered paragraph, carrying the label.
    const std::string expected =
        R"(::: wording

```cpp
int f();
```

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `A` is `true`,
- [#.#]{.pnum} `B` is `false`.

:::
)";
    CHECK(mpark::render_to_string(item) == expected);
}

TEST_CASE("mpark - an itemize with lead-in prose stays in that paragraph") {
    SpecItem item;
    item.decl.signatures.push_back({"int f();", {}});
    DescriptionElement effects;
    effects.kind = ElementKind::Effects;
    effects.paragraphs.push_back({TextInline{"Equivalent to:"}});
    effects.itemize = Itemize{{{TextInline{"one,"}}, {TextInline{"two."}}}};
    item.descr.elements.push_back(std::move(effects));

    const std::string out = mpark::render_to_string(item);
    // A blank line between the lead-in and the list, without which pandoc
    // reads the first bullet as a lazy continuation of the paragraph.
    CHECK(out.find("[#]{.pnum} *Effects*: Equivalent to:\n\n- [#.#]{.pnum} one,\n- [#.#]{.pnum} two.\n") !=
          std::string::npos);
}

TEST_CASE("mpark - same-kind authored itemizes concatenate") {
    SpecItem           item;
    DescriptionElement first;
    first.kind    = ElementKind::Constraints;
    first.itemize = Itemize{{{TextInline{"one,"}}}};
    DescriptionElement second;
    second.kind         = ElementKind::Constraints;
    second.itemize      = Itemize{{{TextInline{"two."}}}};
    item.descr.elements = {std::move(first), std::move(second)};

    const std::string out = mpark::render_to_string(item);
    CHECK(out.find("- [#.#]{.pnum} one,\n- [#.#]{.pnum} two.\n") != std::string::npos);
}

TEST_CASE("mpark - an authored two-dimensional table is a native pipe table") {
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

    const std::string out = mpark::render_to_string(item);
    CHECK(out.find("[#]{.pnum} *Effects*:\n\n"
                   "| | has \\| value | has no value |\n"
                   "|---|---|---|\n"
                   "| **`rhs`** | assigns | initializes |\n"
                   "| **empty** | destroys | no effect |\n"
                   ": [`operator=` effects]{#optional.assign.copy}\n") != std::string::npos);
    const auto first_table  = out.find("{#optional.assign.copy}\n");
    const auto second_table = out.find("{#optional.assign.second}\n");
    REQUIRE(first_table != std::string::npos);
    REQUIRE(second_table != std::string::npos);
    CHECK(first_table < second_table);
}

// An authored element and its derived twin are one description, so the
// label is emitted once for the run — the same fold latex.cpp performs.
TEST_CASE("mpark - adjacent same-kind elements share one label") {
    SpecItem item;
    item.decl.signatures.push_back({"int f();", {}});
    DescriptionElement derived;
    derived.kind    = ElementKind::Mandates;
    derived.derived = true;
    derived.paragraphs.push_back({TextInline{"Derived."}});
    DescriptionElement authored;
    authored.kind = ElementKind::Mandates;
    authored.paragraphs.push_back({TextInline{"Authored."}});
    item.descr.elements.push_back(std::move(derived));
    item.descr.elements.push_back(std::move(authored));

    const std::string out = mpark::render_to_string(item);
    CHECK(out.find("[#]{.pnum} *Mandates*: Derived.\n\n[#]{.pnum} Authored.\n") != std::string::npos);
    CHECK(out.find("*Mandates*: Authored.") == std::string::npos);
}

TEST_CASE("mpark - grouped overloads share one code fence") {
    SpecItem item;
    item.decl.signatures.push_back({"constexpr optional() noexcept;", {}});
    item.decl.signatures.push_back({"constexpr optional(nullopt_t) noexcept;", {}});
    item.descr.elements.push_back({ElementKind::Ensures, {{TextInline{"*this does not contain a value."}}}, {}});

    CHECK(mpark::render_to_string(item).find(
              "```cpp\nconstexpr optional() noexcept;\nconstexpr optional(nullopt_t) noexcept;\n```") !=
          std::string::npos);
}

TEST_CASE("mpark - a decl with no description emits only its fence") {
    SpecItem item;
    item.decl.signatures.push_back({"optional(const optional&) = default;", {}});
    CHECK(mpark::render_to_string(item) ==
          "::: wording\n\n```cpp\noptional(const optional&) = default;\n```\n\n:::\n");
}

TEST_CASE("mpark - a description with no declaration emits no fence") {
    // A class's own wording (design §6): an itemdescr with no itemdecl. The
    // fence would be the empty code block design §9 rejects on a synopsis.
    SpecItem item;
    item.descr.elements.push_back({ElementKind::Remarks, {{TextInline{"A defined class's own description."}}}, {}});

    CHECK(mpark::render_to_string(item) ==
          "::: wording\n\n[#]{.pnum} *Remarks*: A defined class's own description.\n\n:::\n");
}

TEST_CASE("mpark - index entries are dropped") {
    SpecItem item;
    item.decl.signatures.push_back(
        {"optional value;", {{0, 8, SpanKind::LibraryIndex, ""}, {9, 14, SpanKind::LibraryIndex, "optional"}}});
    item.decl.index.push_back({IndexKind::Global, "optional", ""});
    item.decl.index.push_back({IndexKind::Member, "value", "optional"});

    // A paper fragment builds no index; the entries leave no trace at all.
    const std::string out = mpark::render_to_string(item);
    CHECK(out.find("index") == std::string::npos);
    CHECK(out.find("optional value;") != std::string::npos);
}

TEST_CASE("mpark - document with sections, synopsis, and free prose") {
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

    // A nested section descends one heading level. The whole root is one
    // `::: wording` div, which is where `[#]{.pnum}` numbering restarts.
    const std::string expected =
        R"(::: wording

## Observers [optional.observe]{- .sref} {-}

```cpp
constexpr bool has_value() const noexcept;
```

[#]{.pnum} A program that instantiates it is ill-formed.

### Deeper [optional.observe.deep]{- .sref} {-}

:::
)";
    CHECK(mpark::render_to_string(doc) == expected);
}

TEST_CASE("mpark - base heading level is an option") {
    Document doc;
    Section  sec;
    sec.stable_name = "optional.ctor";
    sec.title       = "Constructors";
    doc.nodes.push_back(std::move(sec));

    CHECK(mpark::render_to_string(doc, {.base_heading_level = 4}).find("#### Constructors [optional.ctor]") !=
          std::string::npos);
}

TEST_CASE("mpark - a titleless section emits no double space") {
    Document doc;
    Section  sec;
    sec.stable_name = "optional.ctor";
    doc.nodes.push_back(std::move(sec));

    CHECK(mpark::render_to_string(doc) == "::: wording\n\n## [optional.ctor]{- .sref} {-}\n\n:::\n");
}

// A synopsis holds no numbered paragraph, so wrapping one would leave an empty
// wording div around a code fence.
TEST_CASE("mpark - a top-level synopsis takes no wording div") {
    Document doc;
    doc.nodes.push_back(Synopsis{.name = "optional", .code = {"template<class T> class optional;", {}}, .roster = {}});

    CHECK(mpark::render_to_string(doc) == "```cpp\ntemplate<class T> class optional;\n```\n");
}

TEST_CASE("mpark - each top-level node gets its own wording div") {
    Document doc;
    SpecItem first;
    first.decl.signatures.push_back({"int f();", {}});
    SpecItem second;
    second.decl.signatures.push_back({"int g();", {}});
    doc.nodes.push_back(std::move(first));
    doc.nodes.push_back(std::move(second));

    const std::string out = mpark::render_to_string(doc);
    // Numbering restarts per div, which is what puts a restart at each root.
    CHECK(out == "::: wording\n\n```cpp\nint f();\n```\n\n:::\n\n::: wording\n\n```cpp\nint g();\n```\n\n:::\n");
}

// --- paper mode ------------------------------------------------------------

TEST_CASE("mpark - paper mode wraps the fragment in an add div") {
    SpecItem item;
    item.decl.signatures.push_back({"int f();", {}});
    item.descr.elements.push_back({ElementKind::Effects, {{TextInline{"Does a thing."}}}, {}});

    const std::string expected =
        R"(::: add

::: wording

```cpp
int f();
```

[x]{.pnum} *Effects*: Does a thing.

:::

:::
)";
    CHECK(mpark::render_to_string(item, {.paper_mode = true}) == expected);
    // Off by default: the existing rendering is untouched.
    CHECK(mpark::render_to_string(item).find("::: add") == std::string::npos);
}

// The literal has to advance. `[x]{.pnum}` five times renders five paragraphs
// all numbered "x", because the filter treats a non-`#`, non-decimal part as a
// literal and copies it through -- so the sequence is x, x+1, x+2, which is
// what P4307R0 writes by hand.
TEST_CASE("mpark - added paragraph numbers run x, x+1, x+2") {
    SpecItem item;
    item.decl.signatures.push_back({"int f();", {}});
    item.descr.elements.push_back(
        {ElementKind::Effects, {{TextInline{"First."}}, {TextInline{"Second."}}, {TextInline{"Third."}}}, {}});

    const std::string out = mpark::render_to_string(item, {.paper_mode = true});
    CHECK(out.find("[x]{.pnum} *Effects*: First.") != std::string::npos);
    CHECK(out.find("[x+1]{.pnum} Second.") != std::string::npos);
    CHECK(out.find("[x+2]{.pnum} Third.") != std::string::npos);
    CHECK(out.find("[#]{.pnum}") == std::string::npos);
}

// A sublist item takes its lead-in paragraph's literal with `.#` after it: the
// filter *does* auto-number a `#` part under a literal parent, so these render
// x.1, x.2 -- and not "x.x", which would number every item alike.
TEST_CASE("mpark - an added sublist hangs off its lead-in paragraph's literal") {
    SpecItem item;
    item.decl.signatures.push_back({"int f();", {}});
    DescriptionElement constraints;
    constraints.kind    = ElementKind::Constraints;
    constraints.itemize = Itemize{{{TextInline{"one,"}}, {TextInline{"two."}}}};
    DescriptionElement effects;
    effects.kind = ElementKind::Effects;
    effects.paragraphs.push_back({TextInline{"Then this."}});
    item.descr.elements.push_back(std::move(constraints));
    item.descr.elements.push_back(std::move(effects));

    const std::string out = mpark::render_to_string(item, {.paper_mode = true});
    CHECK(out.find("[x]{.pnum} *Constraints*:") != std::string::npos);
    CHECK(out.find("- [x.#]{.pnum} one,") != std::string::npos);
    CHECK(out.find("- [x.#]{.pnum} two.") != std::string::npos);
    // The sublist does not consume a paragraph number of its own.
    CHECK(out.find("[x+1]{.pnum} *Effects*: Then this.") != std::string::npos);
}

// Numbering restarts per `::: wording` div, exactly as `[#]` does -- one div
// per top-level node, and one `::: add` around the lot.
TEST_CASE("mpark - added numbering restarts per wording div, under one add div") {
    Document doc;
    for (const char* name : {"first", "second"}) {
        Section sec;
        sec.stable_name = name;
        sec.title       = name;
        SpecItem item;
        item.decl.signatures.push_back({"int f();", {}});
        item.descr.elements.push_back({ElementKind::Effects, {{TextInline{"One."}}, {TextInline{"Two."}}}, {}});
        sec.children.push_back(std::move(item));
        doc.nodes.push_back(std::move(sec));
    }

    const std::string out = mpark::render_to_string(doc, {.paper_mode = true});
    // Exactly one editing instruction for the fragment, not one per section.
    CHECK(out.find("::: add") == out.rfind("::: add"));
    // Two "x" starts and two "x+1"s: the counter reset at the second div.
    CHECK(out.find("[x]{.pnum}") != out.rfind("[x]{.pnum}"));
    CHECK(out.find("[x+1]{.pnum}") != out.rfind("[x+1]{.pnum}"));
    CHECK(out.find("[x+2]{.pnum}") == std::string::npos);
}

TEST_CASE("mpark - paper mode leaves a bare synopsis alone") {
    Document doc;
    doc.nodes.push_back(Synopsis{.name = "gadget", .code = {"class gadget;", {}}, .roster = {}});

    // No wording div means no numbering to change; the add div still wraps it,
    // since the declaration is as much a part of the addition as the wording.
    CHECK(mpark::render_to_string(doc, {.paper_mode = true}) == "::: add\n\n```cpp\nclass gadget;\n```\n\n:::\n");
}

// Neither `@` nor `$` is a C++ token, so no corpus header holds one and no
// golden can see this: a unit test is the only guard. The behaviour under test
// is that code text passes through *unaltered* — mpark's scanner has no
// backslash handling, so an escape here would emit a literal backslash, and a
// same-line pair is fixed with the framework's ```cpp {md=none} override
// instead.
TEST_CASE("mpark - a literal @ or $ in code text passes through unescaped") {
    SpecItem item;
    item.decl.signatures.push_back({"int a$b, c@d;", {}});
    const std::string out = mpark::render_to_string(item);
    CHECK(out.find("int a$b, c@d;") != std::string::npos);
    CHECK(out.find('\\') == std::string::npos);
}
