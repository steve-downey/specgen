// tests/beman/specgen/draft_import.test.cpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/backend/latex.hpp>
#include <beman/specgen/draft_import.hpp>
#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using namespace beman::specgen;
using draft_import::import_sections;
using draft_import::Options;

std::string read_corpus(std::string_view name) {
    std::ifstream      in(std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/standard-slices/" + std::string(name),
                          std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool has_diag(const draft_import::Result& r, Severity severity, std::string_view needle) {
    return std::ranges::any_of(r.diagnostics, [&](const draft_import::Diagnostic& d) {
        return d.severity == severity && d.message.find(needle) != std::string::npos;
    });
}

const ir::Section& only_section(const draft_import::Result& r) {
    REQUIRE(r.document.nodes.size() == 1);
    return std::get<ir::Section>(r.document.nodes.front());
}

const ir::SpecItem& item_at(const ir::Section& s, std::size_t i) { return std::get<ir::SpecItem>(s.children.at(i)); }

std::string trim_copy(std::string_view s) {
    const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && is_space(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && is_space(s.back()))
        s.remove_suffix(1);
    return std::string(s);
}

// The layout hints the importer drops: `\brk{}`, `\brk`, `\-`.
std::string drop_hints(std::string text) {
    for (const std::string_view hint : {"\\brk{}", "\\brk", "\\-"})
        for (std::string::size_type at = 0; (at = text.find(hint, at)) != std::string::npos;)
            text.erase(at, hint.size());
    // `\~` is the accent macro the draft writes for a tilde in code; the
    // backend writes the same character as `\textasciitilde{}`.
    for (std::string::size_type at = 0; (at = text.find("\\~", at)) != std::string::npos;)
        text.replace(at, 2, "\\textasciitilde{}");
    return text;
}

// What the importer is allowed to change in a round trip: line breaks inside a
// paragraph, blank lines, `%` line joiners, comment lines, index entries
// that are not library index entries, and `\exposid` against its
// no-italic-correction form inside a listing. Everything else has to come
// back as it went in.
std::vector<std::string> normalize(std::string_view tex) {
    std::vector<std::string> out;
    bool                     verbatim = false;
    bool                     joining  = false; // the previous output line is a prose run
    std::istringstream       in{std::string(tex)};
    for (std::string raw; std::getline(in, raw);) {
        std::string line = trim_copy(raw);
        if (!verbatim && (line.empty() || line.starts_with('%') || line.starts_with("\\index"))) {
            joining = line.empty() && joining ? false : joining;
            continue;
        }
        if (!verbatim && line.ends_with('%'))
            line.pop_back();
        if (!verbatim)
            line = drop_hints(line);
        const bool opens =
            !verbatim && (line.starts_with("\\begin{codeblock}") || line.starts_with("\\begin{itemdecl}"));
        const bool closes = verbatim && (line.starts_with("\\end{codeblock}") || line.starts_with("\\end{itemdecl}"));
        if (verbatim || opens || closes) {
            for (std::string::size_type at = 0; (at = raw.find("\\exposid{", at)) != std::string::npos;)
                raw.replace(at, 9, "\\exposidnc{");
            line = drop_hints(line);
            raw  = drop_hints(raw);
            out.push_back(verbatim && !closes ? raw.substr(0, raw.find_last_not_of(" \t") + 1) : line);
            verbatim = !closes && (verbatim || opens);
            joining  = false;
            continue;
        }
        const bool structural =
            line.starts_with("\\begin{") || line.starts_with("\\end{") || line.starts_with("\\pnum") ||
            line.starts_with("\\rSec") ||
            (line.size() > 1 && line.front() == '\\' && ir::element_from_name(line.substr(1)).has_value());
        if (structural) {
            out.push_back(line);
            joining = false;
            continue;
        }
        if (joining)
            out.back() += ' ' + line;
        else
            out.push_back(line);
        joining = true;
    }
    return out;
}

} // namespace

TEST_CASE("draft_import - a subclause with its subsections, items, and index entries") {
    constexpr std::string_view tex = R"(\rSec2[demo]{Demo}

\rSec3[demo.ctor]{Constructors}

\indexlibraryctor{demo}%
\begin{itemdecl}
demo() noexcept;
demo(nullptr_t) noexcept;
\end{itemdecl}

\begin{itemdescr}
\pnum
\ensures
\tcode{*this} has no target object.
\end{itemdescr}

\rSec3[demo.other]{Other}
\pnum
Text.
\rSec2[next]{Next}
)";
    const auto                 r   = import_sections(tex, Options{{"demo"}});
    CHECK(r.diagnostics.empty());
    const auto& demo = only_section(r);
    CHECK(demo.stable_name == "demo");
    CHECK(demo.title == "Demo");
    REQUIRE(demo.children.size() == 2);

    const auto& ctor = std::get<ir::Section>(demo.children[0]);
    CHECK(ctor.stable_name == "demo.ctor");
    REQUIRE(ctor.children.size() == 1);
    const auto& item = item_at(ctor, 0);
    REQUIRE(item.decl.signatures.size() == 2);
    CHECK(item.decl.signatures[0].text == "demo() noexcept;");
    CHECK(item.decl.signatures[1].text == "demo(nullptr_t) noexcept;");
    REQUIRE(item.decl.index.size() == 1);
    CHECK(item.decl.index[0].kind == ir::IndexKind::Constructor);
    CHECK(item.decl.index[0].name == "demo");
    REQUIRE(item.descr.elements.size() == 1);
    CHECK(item.descr.elements[0].kind == ir::ElementKind::Ensures);

    const auto& other = std::get<ir::Section>(demo.children[1]);
    CHECK(other.stable_name == "demo.other");
}

TEST_CASE("draft_import - a name the source lacks is an error and adds no node") {
    const auto r = import_sections("\\rSec2[a]{A}\n", Options{{"missing", "a"}});
    CHECK(has_diag(r, Severity::Error, "no subclause"));
    REQUIRE(r.document.nodes.size() == 1);
    CHECK(std::get<ir::Section>(r.document.nodes.front()).stable_name == "a");
}

TEST_CASE("draft_import - inline macros become inline kinds") {
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
Meets \oldconcept{Destructible} for \cv{}\tcode{\&} and \placeholder{ref}, see \iref{y} and \libconcept{c};
\tcode{\exposid{is-callable-from}<VT>} and \exposid{z} and \seebelow.
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(r.diagnostics.empty());
    const auto& item = item_at(only_section(r), 0);
    REQUIRE(item.descr.leading.size() == 1);
    const auto& para = std::get<ir::BodyParagraph>(item.descr.leading.front()).text;
    CHECK(std::get<ir::OldConceptRef>(para[1]).name == "Destructible");
    CHECK(std::get<ir::PlaceholderInline>(para[3]).text == "cv");
    const auto& amp = std::get<ir::CodeInline>(para[4]).code;
    CHECK(amp.text == "&");
    CHECK(std::get<ir::PlaceholderInline>(para[6]).text == "ref");
    CHECK(std::get<ir::RefInline>(para[8]).stable_name == "y");
    CHECK(std::get<ir::ConceptRef>(para[10]).name == "c");
    const auto& expos = std::get<ir::CodeInline>(para[12]).code;
    CHECK(expos.text == "is-callable-from<VT>");
    REQUIRE(expos.spans.size() == 1);
    CHECK(expos.spans[0].kind == ir::SpanKind::ExposId);
    CHECK(expos.spans[0].end == 16);
    const auto& bare = std::get<ir::CodeInline>(para[14]).code;
    CHECK(bare.spans.size() == 1);
    CHECK(bare.spans[0].kind == ir::SpanKind::ExposId);
    CHECK(std::get<ir::CodeInline>(para[16]).code.spans[0].kind == ir::SpanKind::SeeBelow);
}

TEST_CASE("draft_import - escapes in a listing become spans, and an unknown escape stays the author's") {
    constexpr std::string_view tex  = R"(\rSec3[x]{X}
\begin{itemdecl}
template<class VT> static constexpr bool @\exposid{is-callable-from}@ = @\seebelow@; // \expos
void f() @\cv{}@ @\added{x}@; // \ref{y}
\end{itemdecl}
)";
    const auto                 r    = import_sections(tex, Options{{"x"}});
    const auto&                sigs = item_at(only_section(r), 0).decl.signatures;
    REQUIRE(sigs.size() == 2);
    REQUIRE(sigs[0].spans.size() == 2);
    CHECK(sigs[0].spans[0].kind == ir::SpanKind::ExposId);
    CHECK(sigs[0].spans[1].kind == ir::SpanKind::SeeBelow);
    CHECK(sigs[0].text.ends_with("; // \\expos"));
    REQUIRE(sigs[1].spans.size() == 2);
    CHECK(sigs[1].spans[0].kind == ir::SpanKind::Placeholder);
    CHECK(sigs[1].spans[0].payload == "cv");
    CHECK(sigs[1].text.find("@\\added{x}@") != std::string::npos);
    CHECK(sigs[1].spans[1].kind == ir::SpanKind::Ref);
    CHECK(sigs[1].spans[1].payload == "y");
}

TEST_CASE("draft_import - elements, Equivalent to, lists, notes, and continuations") {
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\begin{itemdecl}
void f();
\end{itemdecl}
\begin{itemdescr}
\pnum
\effects
Equivalent to:
\begin{codeblock}
return f();
\end{codeblock}

\pnum
\returns
If
\begin{itemize}
\item
a, or
\item
b,
\end{itemize}
then \tcode{x}.
\begin{note}
Careful.
\end{note}
\end{itemdescr}
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(r.diagnostics.empty());
    const auto& descr = item_at(only_section(r), 0).descr;
    REQUIRE(descr.elements.size() == 2);
    CHECK(descr.elements[0].kind == ir::ElementKind::Effects);
    REQUIRE(descr.elements[0].body.size() == 1);
    CHECK(std::get<ir::EquivalentTo>(descr.elements[0].body[0]).code.text == "return f();");

    const auto& returns = descr.elements[1].body;
    REQUIRE(returns.size() == 4);
    CHECK_FALSE(std::get<ir::BodyParagraph>(returns[0]).continuation);
    CHECK(std::get<ir::Itemize>(returns[1]).items.size() == 2);
    CHECK(std::get<ir::BodyParagraph>(returns[2]).continuation);
    CHECK(std::holds_alternative<ir::Note>(returns[3]));
}

TEST_CASE("draft_import - a synopsis names the one class it declares") {
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\begin{codeblock}
namespace std {
  template<class R, class... ArgTypes>
  class demo<R(ArgTypes...)> {
  public:
    demo() noexcept;
  };
}
\end{codeblock}
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    const auto&                syn = std::get<ir::Synopsis>(only_section(r).children.at(0));
    CHECK(syn.name == "demo");
    CHECK(syn.code.text.starts_with("namespace std {"));
}

TEST_CASE("draft_import - what the IR has no kind for is kept as raw LaTeX and reported") {
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
A \term{thing} costs $O(n)$.
\begin{tokens}
foo
\end{tokens}
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(has_diag(r, Severity::Warning, "\\term has no IR kind"));
    CHECK(has_diag(r, Severity::Warning, "mathematics"));
    CHECK(has_diag(r, Severity::Warning, "tokens environment"));
    for (const auto& d : r.diagnostics)
        CHECK(d.section == "x");
    const auto& para = std::get<ir::BodyParagraph>(item_at(only_section(r), 0).descr.leading.front()).text;
    CHECK(std::holds_alternative<ir::RawInline>(para[1]));
    CHECK(std::get<ir::RawInline>(para[1]).latex == "\\term{thing}");
}

TEST_CASE("draft_import - the corpus slices round-trip through the LaTeX backend") {
    for (const auto& [file, section] : {std::pair{"func.wrap.move.slice", "func.wrap.move"},
                                        std::pair{"optional.ctor.slice", "optional.ctor"},
                                        std::pair{"optional.assign.slice", "optional.assign"}}) {
        CAPTURE(file);
        const std::string source = read_corpus(file);
        REQUIRE_FALSE(source.empty());
        const auto r = import_sections(source, Options{{section}});
        REQUIRE_FALSE(std::ranges::any_of(r.diagnostics, [](const auto& d) { return d.severity == Severity::Error; }));
        const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
        const auto        want     = normalize(source);
        const auto        got      = normalize(rendered);
        // Compare in step and stop at the first difference, so a mismatch
        // names the line rather than only the count.
        const auto [g, w] = std::ranges::mismatch(got, want);
        if (g != got.end() && w != want.end()) {
            CHECK(*g == *w);
        }
        CHECK(got.size() == want.size());
    }
}

TEST_CASE("draft_import - a library table, and a reference to it") {
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\begin{itemdecl}
void f();
\end{itemdecl}
\begin{itemdescr}
\pnum
\effects
See \tref{x.tab}.
\begin{lib2dtab2}{\tcode{f} effects}{x.tab}
{\tcode{a} is set}
{\tcode{a} is not set}

\rowhdr{\tcode{b} is set} &
assigns \& more &
no effect \\
\rowsep

\rowhdr{\tcode{b} is not set} &
destroys it &
creates it \\
\end{lib2dtab2}
\begin{libtab2}{names}{x.flat}{lp{4.5in}}{Name}{Meaning}
\tcode{one} & first \\ \rowsep
\tcode{two} & second \\
\end{libtab2}
\end{itemdescr}
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(r.diagnostics.empty());
    const auto& body = item_at(only_section(r), 0).descr.elements.at(0).body;
    REQUIRE(body.size() == 3);
    const auto& ref = std::get<ir::BodyParagraph>(body[0]).text;
    CHECK(std::get<ir::TableRef>(ref[1]).stable_name == "x.tab");

    const auto& grid = std::get<ir::Table2D>(body[1]);
    CHECK(grid.stable_name == "x.tab");
    REQUIRE(grid.rows.size() == 2);
    CHECK(std::get<ir::CodeInline>(grid.rows[0].header[0]).code.text == "b");
    // A cell's `&` is plain text again: the LaTeX backend escapes it.
    CHECK(std::get<ir::TextInline>(grid.rows[0].cell1[0]).text == "assigns & more");
    CHECK(std::get<ir::TextInline>(grid.rows[1].cell2[0]).text == "creates it");

    const auto& flat = std::get<ir::Table1D>(body[2]);
    CHECK(flat.stable_name == "x.flat");
    REQUIRE(flat.rows.size() == 2);
    CHECK(std::get<ir::TextInline>(flat.rows[1].cell2[0]).text == "second");
}
