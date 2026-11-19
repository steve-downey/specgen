// tests/beman/specgen/backend/draft_edit.test.cpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/backend/draft_edit.hpp>
#include <beman/specgen/backend/draft_edit.hpp> // Re-inclusion verification

#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/draft_import.hpp>
#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace draft_edit = beman::specgen::backend::draft_edit;
namespace ir         = beman::specgen::ir;
using beman::specgen::draft_import::import_sections;

namespace {

// The draft's source: a synopsis and two items, laid out by hand, with an
// index entry and a comment that the edit must leave alone.
constexpr std::string_view kDraft = R"(\rSec2[w]{Wrapper}

\begin{codeblock}
namespace std {
  template<class R>
  class wrapper {
  public:
    wrapper() noexcept;
    R operator()() @\cv{}@;   // \expos
    void reset();
  };
}
\end{codeblock}

\indexlibraryctor{wrapper}%
\begin{itemdecl}
wrapper() noexcept;
\end{itemdecl}

\begin{itemdescr}
\pnum
\effects
Constructs an empty wrapper.% a comment
\end{itemdescr}

\indexlibrarymember{operator()}{wrapper}%
\begin{itemdecl}
template<class... Args>
  R operator()(Args&&... args) @\cv{}@;
\end{itemdecl}

\begin{itemdescr}
\pnum
\returns
The result.
\end{itemdescr}
)";

ir::Document imported() { return import_sections(kDraft, {.sections = {"w"}, .source_name = "w.tex"}).document; }

std::vector<ir::Node>& children(ir::Document& doc) { return std::get<ir::Section>(doc.nodes.front()).children; }

std::string applied(const draft_edit::Plan& plan) {
    if (plan.files.empty())
        return std::string(kDraft);
    REQUIRE(plan.files.size() == 1);
    CHECK(plan.files.front().file == "w.tex");
    const auto out = draft_edit::apply(kDraft, plan.files.front().edits);
    REQUIRE(out.has_value());
    return *out;
}

} // namespace

TEST_CASE("draft_edit - the draft against itself is no edit and no finding") {
    const draft_edit::Plan plan = draft_edit::plan(imported(), imported());
    CHECK(plan.files.empty());
    CHECK(plan.findings.empty());
}

TEST_CASE("draft_edit - an index entry the draft does not have is no edit") {
    // An extracted class head is `\libglobal{wrapper}`; the draft's is plain
    // (issue #354).
    ir::Document      paper    = imported();
    auto&             synopsis = std::get<ir::Synopsis>(children(paper)[0]).code;
    const std::size_t at       = synopsis.text.find("class wrapper") + 6;
    synopsis.spans.push_back({at, at + 7, ir::SpanKind::LibraryIndex, ""});
    std::ranges::sort(synopsis.spans, {}, &ir::Span::begin);
    const draft_edit::Plan plan = draft_edit::plan(imported(), paper);
    CHECK(plan.findings.empty());
    CHECK(plan.files.empty());
}

TEST_CASE("draft_edit - an added specifier is an edit of that token alone, in the draft's own layout") {
    ir::Document paper = imported();
    auto&        kids  = children(paper);
    // The paper's code laid out its own way: one line, other indentation.
    std::get<ir::SpecItem>(kids[1]).decl.signatures.front() =
        beman::specgen::draft_import::parse_listing("constexpr wrapper() noexcept;");
    std::get<ir::SpecItem>(kids[2]).decl.signatures.front() = beman::specgen::draft_import::parse_listing(
        "template<class... Args> constexpr R operator()(Args&&... args) @\\cv{}@;");
    auto& synopsis = std::get<ir::Synopsis>(kids[0]).code;
    synopsis       = beman::specgen::draft_import::parse_listing(
        "namespace std {\n  template<class R>\n  class wrapper {\n  public:\n    constexpr wrapper() noexcept;\n"
        "    constexpr R operator()() @\\cv{}@;   // \\expos\n    void reset();\n  };\n}");

    const draft_edit::Plan plan = draft_edit::plan(imported(), paper);
    CHECK(plan.findings.empty());
    REQUIRE(plan.files.size() == 1);
    CHECK(plan.files.front().edits.size() == 4);
    const std::string out = applied(plan);

    std::string expected(kDraft);
    const auto  replace = [&](std::string_view from, std::string_view to) {
        const auto at = expected.find(from);
        REQUIRE(at != std::string::npos);
        expected.replace(at, from.size(), to);
    };
    replace("    wrapper() noexcept;", "    constexpr wrapper() noexcept;");
    replace("    R operator()() @\\cv{}@;", "    constexpr R operator()() @\\cv{}@;");
    replace("\nwrapper() noexcept;\n", "\nconstexpr wrapper() noexcept;\n");
    replace("  R operator()(Args&&... args)", "  constexpr R operator()(Args&&... args)");
    CHECK(out == expected);
}

TEST_CASE("draft_edit - a removed token takes its space with it") {
    ir::Document paper = imported();
    std::get<ir::SpecItem>(children(paper)[1]).decl.signatures.front() =
        beman::specgen::draft_import::parse_listing("wrapper();");
    const std::string out = applied(draft_edit::plan(imported(), paper));
    CHECK(out.contains("\\begin{itemdecl}\nwrapper();\n\\end{itemdecl}"));
    // The synopsis's declaration is another one, and stays.
    CHECK(out.contains("    wrapper() noexcept;"));
}

// The draft's source, for the edits that read it.
std::optional<std::string> read_draft(const std::string& file) {
    return file == "w.tex" ? std::optional<std::string>(kDraft) : std::nullopt;
}

TEST_CASE("draft_edit - a changed description replaces the paragraph that changed, laid out as the draft's") {
    ir::Document paper               = imported();
    auto&        item                = std::get<ir::SpecItem>(children(paper)[1]);
    item.descr.elements.front().body = {
        ir::BodyParagraph{{ir::TextInline{"Constructs a wrapper with no target. It is empty."}}}};

    const draft_edit::Plan plan = draft_edit::plan(imported(), paper, read_draft);
    CHECK(plan.findings.empty());
    REQUIRE(plan.files.size() == 1);
    REQUIRE(plan.files.front().edits.size() == 1);
    const std::string out = applied(plan);
    CHECK(out.contains("\\begin{itemdescr}\n\\pnum\n\\effects\nConstructs a wrapper with no target.\nIt is empty.\n"
                       "\\end{itemdescr}"));
    // The other item's description is the draft's, as it was.
    CHECK(out.contains("\\pnum\n\\returns\nThe result.\n\\end{itemdescr}"));
}

TEST_CASE("draft_edit - an item the paper adds goes after the one before it, and one it removes goes") {
    ir::Document paper = imported();
    ir::SpecItem added;
    added.decl.signatures.push_back({"void clear();", {}});
    ir::DescriptionElement effects;
    effects.kind = ir::ElementKind::Effects;
    effects.body = {ir::BodyParagraph{{ir::TextInline{"Clears it."}}}};
    added.descr.elements.push_back(std::move(effects));
    children(paper).insert(children(paper).begin() + 2, added);
    // The paper drops the call operator's item.
    children(paper).erase(children(paper).begin() + 3);

    const draft_edit::Plan plan = draft_edit::plan(imported(), paper, read_draft);
    CHECK(plan.findings.empty());
    const std::string out = applied(plan);
    CHECK(
        out.contains("Constructs an empty wrapper.% a comment\n\\end{itemdescr}\n\n\\begin{itemdecl}\nvoid clear();\n"
                     "\\end{itemdecl}\n\n\\begin{itemdescr}\n\\pnum\n\\effects\nClears it.\n\\end{itemdescr}\n"));
    CHECK_FALSE(out.contains("\\indexlibrarymember{operator()}{wrapper}"));
    CHECK_FALSE(out.contains("The result."));
}

TEST_CASE("draft_edit - a change it cannot place is an error, and nothing is placed for it") {
    ir::Document paper = imported();
    auto&        item  = std::get<ir::SpecItem>(children(paper)[1]);
    item.decl.signatures.push_back({"wrapper(int);", {}});
    children(paper).push_back(ir::Section{"w.new", "New", {}});

    // Without the draft's source neither can be placed: a declaration goes in
    // beside the lines of its neighbour, and a subclause takes the level of
    // the one it is in, both of which are read from it.
    const draft_edit::Plan plan = draft_edit::plan(imported(), paper);
    CHECK(plan.files.empty());
    REQUIRE(plan.findings.size() == 2);
    CHECK(plan.findings[0].severity == beman::specgen::Severity::Error);
    CHECK(plan.findings[0].where == "w: `wrapper() noexcept;`");
    CHECK(plan.findings[0].message.contains("adds a declaration"));
    CHECK(plan.findings[1].message.contains("adds the subclause [w.new]"));
}

TEST_CASE("draft_edit - edits of one file from several documents are merged, each once") {
    ir::Document paper = imported();
    std::get<ir::SpecItem>(children(paper)[1]).decl.signatures.front() =
        beman::specgen::draft_import::parse_listing("constexpr wrapper() noexcept;");
    std::vector<draft_edit::Plan> plans;
    plans.push_back(draft_edit::plan(imported(), paper));
    plans.push_back(draft_edit::plan(imported(), paper));
    const draft_edit::Plan merged = draft_edit::merge(std::move(plans));
    REQUIRE(merged.files.size() == 1);
    CHECK(merged.files.front().edits.size() == 1);
}

TEST_CASE("draft_edit - overlapping edits are refused") {
    const std::vector<draft_edit::Edit> edits = {{0, 4, "x"}, {2, 6, "y"}};
    CHECK_FALSE(draft_edit::apply("abcdefgh", edits).has_value());
    const std::vector<draft_edit::Edit> fine = {{6, 6, "Z"}, {0, 1, ""}};
    CHECK(draft_edit::apply("abcdefgh", fine) == "bcdefZgh");
}

namespace {

// A second draft for the edits of issue #346: two subclauses, an item with
// two declarations and a comment above it, and a paragraph of three
// sentences, each on its own line as the draft writes them.
constexpr std::string_view kVector = R"(\rSec2[v]{Vector}

\rSec3[v.cons]{Constructors}

% The constructors.
\indexlibraryctor{v}%
\begin{itemdecl}
v() noexcept;
v(int n);
\end{itemdecl}

\begin{itemdescr}
\pnum
\effects
Constructs an empty vector.
Its size is zero.
The capacity is unspecified.
\end{itemdescr}

\rSec3[v.cap]{Capacity}

\indexlibrarymember{size}{v}%
\begin{itemdecl}
int size() const;
\end{itemdecl}

\begin{itemdescr}
\pnum
\returns
The size.
\end{itemdescr}
)";

ir::Document vector_draft() { return import_sections(kVector, {.sections = {"v"}, .source_name = "v.tex"}).document; }

std::optional<std::string> read_vector(const std::string& file) {
    return file == "v.tex" ? std::optional<std::string>(kVector) : std::nullopt;
}

ir::Section& subclause(ir::Document& doc, std::size_t i) {
    return std::get<ir::Section>(std::get<ir::Section>(doc.nodes.front()).children.at(i));
}

std::string applied_vector(const draft_edit::Plan& plan) {
    CHECK(plan.findings.empty());
    REQUIRE(plan.files.size() == 1);
    const auto out = draft_edit::apply(kVector, plan.files.front().edits);
    REQUIRE(out.has_value());
    return *out;
}

} // namespace

TEST_CASE("draft_edit - a changed sentence replaces its line, and the paragraph's other lines are the draft's") {
    ir::Document paper               = vector_draft();
    auto&        item                = std::get<ir::SpecItem>(subclause(paper, 0).children.front());
    item.descr.elements.front().body = {ir::BodyParagraph{
        {ir::TextInline{"Constructs an empty vector. Its size is 0. The capacity is unspecified."}}}};

    const draft_edit::Plan plan = draft_edit::plan(vector_draft(), paper, read_vector);
    REQUIRE(plan.files.size() == 1);
    REQUIRE(plan.files.front().edits.size() == 1);
    CHECK(plan.files.front().edits.front().text == "Its size is 0.");
    CHECK(applied_vector(plan).contains(
        "\\effects\nConstructs an empty vector.\nIts size is 0.\nThe capacity is unspecified.\n\\end{itemdescr}"));
}

TEST_CASE("draft_edit - a sentence added or removed is a line added or removed") {
    ir::Document added               = vector_draft();
    auto&        more                = std::get<ir::SpecItem>(subclause(added, 0).children.front());
    more.descr.elements.front().body = {ir::BodyParagraph{{ir::TextInline{
        "Constructs an empty vector. It allocates nothing. Its size is zero. The capacity is unspecified."}}}};
    CHECK(applied_vector(draft_edit::plan(vector_draft(), added, read_vector))
              .contains("Constructs an empty vector.\nIt allocates nothing.\nIts size is zero.\n"));

    ir::Document removed              = vector_draft();
    auto&        fewer                = std::get<ir::SpecItem>(subclause(removed, 0).children.front());
    fewer.descr.elements.front().body = {
        ir::BodyParagraph{{ir::TextInline{"Constructs an empty vector. The capacity is unspecified."}}}};
    CHECK(applied_vector(draft_edit::plan(vector_draft(), removed, read_vector))
              .contains("\\effects\nConstructs an empty vector.\nThe capacity is unspecified.\n\\end{itemdescr}"));
}

TEST_CASE("draft_edit - a declaration joining an item is a line after its neighbour, and one leaving takes its line") {
    ir::Document joined = vector_draft();
    auto&        more   = std::get<ir::SpecItem>(subclause(joined, 0).children.front());
    more.decl.signatures.push_back(beman::specgen::draft_import::parse_listing("v(int n, int m);"));
    CHECK(applied_vector(draft_edit::plan(vector_draft(), joined, read_vector))
              .contains("\\begin{itemdecl}\nv() noexcept;\nv(int n);\nv(int n, int m);\n\\end{itemdecl}"));

    ir::Document first = vector_draft();
    auto&        ahead = std::get<ir::SpecItem>(subclause(first, 0).children.front());
    ahead.decl.signatures.insert(ahead.decl.signatures.begin(),
                                 beman::specgen::draft_import::parse_listing("explicit v(char c);"));
    CHECK(applied_vector(draft_edit::plan(vector_draft(), first, read_vector))
              .contains("\\begin{itemdecl}\nexplicit v(char c);\nv() noexcept;\n"));

    ir::Document left  = vector_draft();
    auto&        fewer = std::get<ir::SpecItem>(subclause(left, 0).children.front());
    fewer.decl.signatures.pop_back();
    CHECK(applied_vector(draft_edit::plan(vector_draft(), left, read_vector))
              .contains("\\begin{itemdecl}\nv() noexcept;\n\\end{itemdecl}"));
}

TEST_CASE("draft_edit - a removed item takes the comment directly above it") {
    ir::Document paper = vector_draft();
    subclause(paper, 0).children.clear();
    const std::string out = applied_vector(draft_edit::plan(vector_draft(), paper, read_vector));
    CHECK_FALSE(out.contains("% The constructors."));
    CHECK_FALSE(out.contains("\\indexlibraryctor{v}"));
    CHECK(out.contains("\\rSec3[v.cons]{Constructors}\n\n\\rSec3[v.cap]{Capacity}"));
}

TEST_CASE("draft_edit - a subclause added goes after its predecessor at its level, and one removed goes") {
    ir::Document paper = vector_draft();
    ir::SpecItem clear;
    clear.decl.signatures.push_back({"void clear();", {}});
    ir::DescriptionElement effects;
    effects.kind = ir::ElementKind::Effects;
    effects.body = {ir::BodyParagraph{{ir::TextInline{"Removes every element."}}}};
    clear.descr.elements.push_back(std::move(effects));
    std::get<ir::Section>(paper.nodes.front()).children.push_back(ir::Section{"v.mod", "Modifiers", {clear}});
    const std::string out = applied_vector(draft_edit::plan(vector_draft(), paper, read_vector));
    CHECK(out.contains("The size.\n\\end{itemdescr}\n\n\\rSec3[v.mod]{Modifiers}\n"));
    CHECK(out.contains("\\begin{itemdecl}\nvoid clear();\n\\end{itemdecl}"));

    ir::Document fewer = vector_draft();
    auto&        kids  = std::get<ir::Section>(fewer.nodes.front()).children;
    kids.erase(kids.begin() + 1);
    const std::string gone = applied_vector(draft_edit::plan(vector_draft(), fewer, read_vector));
    CHECK_FALSE(gone.contains("\\rSec3[v.cap]"));
    CHECK_FALSE(gone.contains("The size."));
    CHECK(gone.contains("The capacity is unspecified.\n\\end{itemdescr}\n"));
}
