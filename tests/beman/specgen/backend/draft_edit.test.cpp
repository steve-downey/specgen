// tests/beman/specgen/backend/draft_edit.test.cpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/backend/draft_edit.hpp>
#include <beman/specgen/backend/draft_edit.hpp> // Re-inclusion verification

#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/draft_import.hpp>
#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("draft_edit - a change it cannot place is an error, and nothing is placed for it") {
    ir::Document paper               = imported();
    auto&        item                = std::get<ir::SpecItem>(children(paper)[1]);
    item.descr.elements.front().body = {ir::BodyParagraph{{ir::TextInline{"Constructs a wrapper with no target."}}}};
    children(paper).push_back(ir::FreeParagraph{{ir::TextInline{"A new paragraph."}}});

    const draft_edit::Plan plan = draft_edit::plan(imported(), paper);
    CHECK(plan.files.empty());
    REQUIRE(plan.findings.size() == 2);
    CHECK(plan.findings[0].severity == beman::specgen::Severity::Error);
    CHECK(plan.findings[0].where == "w: `wrapper() noexcept;`");
    CHECK(plan.findings[0].message.contains("description"));
    CHECK(plan.findings[1].message.contains("adds a paragraph"));
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
