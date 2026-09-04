// tests/beman/specgen/backend/latex.test.cpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/backend/latex.hpp>
#include <beman/specgen/backend/latex.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace latex = beman::specgen::backend::latex;
using namespace beman::specgen::ir;

TEST_CASE("latex - HeaderIsIdempotent") {
    // Placeholder: verifies header re-inclusion safety and build coherency.
    // This test always passes if the file compiles.
    REQUIRE(true);
}

// The end-to-end acceptance shape: hand-built IR for [optional.observe]/value_or
// rendering to wording that would drop into the draft sources unchanged.
TEST_CASE("latex - value_or golden") {
    SpecItem item;
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
        R"(\indexlibrarymember{value_or}{optional}%
\begin{itemdecl}
template<class U = remove_cv_t<T>> constexpr remove_cv_t<T> value_or(U&& v) const &;
\end{itemdecl}

\begin{itemdescr}
\pnum
\mandates
\tcode{is_copy_constructible_v<T>} is \tcode{true} and \tcode{is_convertible_v<U, T>} is \tcode{true}.

\pnum
\effects
Equivalent to:
\begin{codeblock}
return has_value() ? **this : static_cast<remove_cv_t<T>>(std::forward<U>(v));
\end{codeblock}
\end{itemdescr}
)";

    CHECK(latex::render_to_string(item) == expected);
}

TEST_CASE("latex - spans become macros, in codeblock and in tcode") {
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

    const std::string out = latex::render_to_string(item);
    // Inside a codeblock the draft escapes back into LaTeX with @...@.
    CHECK(out.find("T @\\exposidnc{val}@;") != std::string::npos);
    // In prose we are already in LaTeX, so no @ delimiters — and a code inline
    // that is *entirely* one span renders as the bare macro: \seebelow is
    // italic prose and \exposid already sets the code font, so a \tcode wrapper
    // would double up.
    CHECK(out.find("\\seebelow") != std::string::npos);
    CHECK(out.find("\\tcode{\\seebelow}") == std::string::npos);
    CHECK(out.find("\\iref{optional.general}") != std::string::npos);
}

TEST_CASE("latex - implementation-defined spans use impdef") {
    SpecItem item;
    item.decl.signatures.push_back(
        {"using iterator = implementation-defined;", {{17, 39, SpanKind::ImplDefined, ""}}});
    CHECK(latex::render_to_string(item).find("using iterator = @\\impdef@;") != std::string::npos);
}

// The Ref *span* and the RefInline are two different cross-references
// and must not share one mapping.
// A span sits in a code comment, where the draft writes
// a bare `\ref` -- and writes it *without* @...@ delimiters, because
// `macros.tex` sets the listings option `texcl=true` and a comment is
// therefore already in TeX mode. Getting either half wrong typesets visible
// garbage in the draft, and no golden covers it: no generate-mode case renders
// to .tex, so this is the only thing watching.
TEST_CASE("latex - a ref span in a comment is bare, not @-escaped or \\iref") {
    Document doc;
    doc.nodes.push_back(Synopsis{
        .name   = "optional",
        .code   = {"class optional {\n  // [optional.ctor], constructors\n  optional();\n};",
                   {{22, 37, SpanKind::Ref, "optional.ctor"}}},
        .roster = {},
    });

    const std::string out = latex::render_to_string(doc);
    CHECK(out.find("// \\ref{optional.ctor}, constructors") != std::string::npos);
    CHECK(out.find("@\\ref{optional.ctor}@") == std::string::npos);
    CHECK(out.find("\\iref{optional.ctor}") == std::string::npos);
}

// The prose cross-reference keeps \iref -- the parenthesized form -- which is
// the other half of that split.
TEST_CASE("latex - a RefInline in prose is still iref") {
    SpecItem           item;
    DescriptionElement remarks;
    remarks.kind = ElementKind::Remarks;
    remarks.paragraphs.push_back({TextInline{"See "}, RefInline{"optional.general"}, TextInline{"."}});
    item.descr.elements.push_back(std::move(remarks));

    CHECK(latex::render_to_string(item).find("See \\iref{optional.general}.") != std::string::npos);
}

TEST_CASE("latex - a partially-spanned code inline keeps its tcode wrapper") {
    SpecItem           item;
    DescriptionElement remarks;
    remarks.kind = ElementKind::Remarks;
    // Only "VAL" is exposition-only; the surrounding member access is not, so
    // the inline still needs \tcode around the whole thing.
    remarks.paragraphs.push_back(
        {TextInline{"Equivalent to "}, CodeInline{{"x.VAL", {{2, 5, SpanKind::ExposId, "val"}}}}});
    item.descr.elements.push_back(std::move(remarks));

    const std::string out = latex::render_to_string(item);
    CHECK(out.find("\\tcode{x.\\exposid{val}}") != std::string::npos);
}

TEST_CASE("latex - every element kind renders as the draft macro of that name") {
    for (int i = 0; i <= static_cast<int>(ElementKind::Errors); ++i) {
        const auto kind = static_cast<ElementKind>(i);
        SpecItem   item;
        item.decl.signatures.push_back({"int f();", {}});
        item.descr.elements.push_back({kind, {{TextInline{"Prose."}}}, {}});

        const std::string out      = latex::render_to_string(item);
        const std::string expected = "\\" + std::string(element_name(kind)) + "\n";
        INFO("kind: " << element_name(kind));
        CHECK(out.find(expected) != std::string::npos);
    }
}

TEST_CASE("latex - multi-paragraph element repeats pnum but not the macro") {
    SpecItem item;
    item.decl.signatures.push_back({"int f();", {}});
    item.descr.elements.push_back({ElementKind::Effects, {{TextInline{"First."}}, {TextInline{"Second."}}}, {}});

    const std::string expected =
        R"(\begin{itemdecl}
int f();
\end{itemdecl}

\begin{itemdescr}
\pnum
\effects
First.

\pnum
Second.
\end{itemdescr}
)";
    CHECK(latex::render_to_string(item) == expected);
}

TEST_CASE("latex - same-kind authored itemizes concatenate") {
    SpecItem           item;
    DescriptionElement first;
    first.kind    = ElementKind::Constraints;
    first.itemize = Itemize{{{TextInline{"one,"}}}};
    DescriptionElement second;
    second.kind         = ElementKind::Constraints;
    second.itemize      = Itemize{{{TextInline{"two."}}}};
    item.descr.elements = {std::move(first), std::move(second)};

    const std::string out = latex::render_to_string(item);
    CHECK(out.find("\\item one,\n\\item two.\n") != std::string::npos);
}

TEST_CASE("latex - an authored two-dimensional table uses lib2dtab2") {
    DescriptionElement effects;
    effects.kind       = ElementKind::Effects;
    effects.paragraphs = {{TextInline{"See the following table."}}};
    effects.table      = Table2D{
        .stable_name = "optional.assign.copy",
        .caption     = {CodeInline{{"optional::operator=(const optional&)", {}}}, TextInline{" effects"}},
        .column1     = {CodeInline{{"*this", {}}}, TextInline{" has a value"}},
        .column2     = {CodeInline{{"*this", {}}}, TextInline{" has no value"}},
        .rows        = {Table2DRow{.header = {CodeInline{{"rhs", {}}}, TextInline{" has a value"}},
                                   .cell1  = {TextInline{"assigns "}, CodeInline{{"rhs.val", {}}}},
                                   .cell2  = {TextInline{"initializes "}, CodeInline{{"val", {}}}}},
                        Table2DRow{.header = {CodeInline{{"rhs", {}}}, TextInline{" has no value"}},
                                   .cell1  = {TextInline{"destroys the value"}},
                                   .cell2  = {TextInline{"no effect"}}}},
    };
    SpecItem item;
    item.decl.signatures.push_back({"optional& operator=(const optional& rhs);", {}});
    DescriptionElement second = effects;
    second.paragraphs.clear();
    second.table->stable_name = "optional.assign.second";
    item.descr.elements.push_back(std::move(effects));
    item.descr.elements.push_back(std::move(second));

    const std::string out = latex::render_to_string(item);
    CHECK(out.find("\\begin{lib2dtab2}{\\tcode{optional::operator=(const optional\\&)} effects}"
                   "{optional.assign.copy}\n"
                   "{\\tcode{*this} has a value}\n"
                   "{\\tcode{*this} has no value}\n\n"
                   "\\rowhdr{\\tcode{rhs} has a value} &\n"
                   "assigns \\tcode{rhs.val} &\n"
                   "initializes \\tcode{val} \\\\\n"
                   "\\rowsep\n"
                   "\n"
                   "\\rowhdr{\\tcode{rhs} has no value} &\n"
                   "destroys the value &\n"
                   "no effect \\\\\n"
                   "\\end{lib2dtab2}\n") != std::string::npos);
    const auto first_table  = out.find("{optional.assign.copy}\n");
    const auto second_table = out.find("{optional.assign.second}\n");
    REQUIRE(first_table != std::string::npos);
    REQUIRE(second_table != std::string::npos);
    CHECK(first_table < second_table);
}

TEST_CASE("latex - grouped overloads share one itemdecl block") {
    SpecItem item;
    item.decl.signatures.push_back({"constexpr optional() noexcept;", {}});
    item.decl.signatures.push_back({"constexpr optional(nullopt_t) noexcept;", {}});
    item.descr.elements.push_back({ElementKind::Ensures, {{TextInline{"*this does not contain a value."}}}, {}});

    const std::string out = latex::render_to_string(item);
    CHECK(out.find("\\begin{itemdecl}\nconstexpr optional() noexcept;\nconstexpr "
                   "optional(nullopt_t) noexcept;\n\\end{itemdecl}") != std::string::npos);
}

TEST_CASE("latex - a decl with no description emits no itemdescr") {
    SpecItem item;
    item.decl.signatures.push_back({"optional(const optional&) = default;", {}});
    const std::string out = latex::render_to_string(item);
    CHECK(out.find("itemdescr") == std::string::npos);
    CHECK(out.find("\\end{itemdecl}") != std::string::npos);
}

TEST_CASE("latex - a description with no declaration emits no itemdecl") {
    // A class's own wording (design §6): an itemdescr with no itemdecl. An
    // empty `itemdecl` environment would be the blank box design §9 rejects
    // on a synopsis.
    SpecItem item;
    item.descr.elements.push_back({ElementKind::Remarks, {{TextInline{"A defined class's own description."}}}, {}});

    const std::string out = latex::render_to_string(item);
    CHECK(out.find("itemdecl") == std::string::npos);
    CHECK(out == "\\begin{itemdescr}\n\\pnum\n\\remarks\nA defined class's own description.\n\\end{itemdescr}\n");
}

TEST_CASE("latex - index entry kinds") {
    SpecItem item;
    item.decl.signatures.push_back({"int f();", {}});
    item.decl.index.push_back({IndexKind::Global, "swap", ""});
    item.decl.index.push_back({IndexKind::Constructor, "optional", ""});
    item.decl.index.push_back({IndexKind::Destructor, "optional", ""});
    item.decl.index.push_back({IndexKind::Member, "value", "optional"});
    item.decl.index.push_back({IndexKind::MemberX, "value_type", "optional"});
    item.decl.index.push_back({IndexKind::MemberExpos, "val", "optional"});
    item.decl.index.push_back({IndexKind::Zombie, "auto_ptr", ""});
    item.decl.index.push_back({IndexKind::Misc, "optional", "class template"});

    const std::string out = latex::render_to_string(item);
    CHECK(out.find("\\indexlibraryglobal{swap}%\n") != std::string::npos);
    CHECK(out.find("\\indexlibraryctor{optional}%\n") != std::string::npos);
    CHECK(out.find("\\indexlibrarydtor{optional}%\n") != std::string::npos);
    CHECK(out.find("\\indexlibrarymember{value}{optional}%\n") != std::string::npos);
    CHECK(out.find("\\indexlibrarymemberx{value_type}{optional}%\n") != std::string::npos);
    CHECK(out.find("\\indexlibrarymemberexpos{val}{optional}%\n") != std::string::npos);
    CHECK(out.find("\\indexlibraryzombie{auto_ptr}%\n") != std::string::npos);
    CHECK(out.find("\\indexlibrarymisc{optional}{class template}%\n") != std::string::npos);

    Document doc;
    doc.nodes.push_back(Synopsis{
        .name = {},
        .code = {"optional value;", {{0, 8, SpanKind::LibraryIndex, ""}, {9, 14, SpanKind::LibraryIndex, "optional"}}},
        .roster = {}});
    const std::string synopsis = latex::render_to_string(doc);
    CHECK(synopsis.find("@\\libglobal{optional}@ @\\libmember{value}{optional}@;") != std::string::npos);
}

TEST_CASE("latex - document with sections, synopsis, and free prose") {
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

    const std::string expected =
        R"(\rSec3[optional.observe]{Observers}

\begin{codeblock}
constexpr bool has_value() const noexcept;
\end{codeblock}

\pnum
A program that instantiates it is ill-formed.

\rSec4[optional.observe.deep]{Deeper}
)";
    CHECK(latex::render_to_string(doc) == expected);
}

TEST_CASE("latex - base section depth is configurable") {
    Document doc;
    Section  sec;
    sec.stable_name = "optional";
    sec.title       = "Optional objects";
    doc.nodes.push_back(std::move(sec));

    latex::Options options;
    options.base_section_depth = 2;
    CHECK(latex::render_to_string(doc, options) == "\\rSec2[optional]{Optional objects}\n");
}
