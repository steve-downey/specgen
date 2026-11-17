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

// `\~` is the accent macro the draft writes for a tilde in code; the backend
// writes the same character as `\textasciitilde{}`.
std::string one_tilde_spelling(std::string text) {
    for (std::string::size_type at = 0; (at = text.find("\\~", at)) != std::string::npos;)
        text.replace(at, 2, "\\textasciitilde{}");
    return text;
}

// What the importer is allowed to change in a round trip: line breaks inside a
// paragraph, blank lines, `%` line joiners, comment lines, index entries
// that are not library index entries, and the spelling of a tilde in code.
// Everything else has to come back as it went in, the line-break hints inside
// code and the choice between a macro and its no-italic-correction twin
// included.
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
            line = one_tilde_spelling(line);
        const bool opens =
            !verbatim && (line.starts_with("\\begin{codeblock}") || line.starts_with("\\begin{itemdecl}"));
        const bool closes = verbatim && (line.starts_with("\\end{codeblock}") || line.starts_with("\\end{itemdecl}"));
        if (verbatim || opens || closes) {
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
    CHECK(std::get<ir::RefInline>(para[8]) == ir::RefInline{"y"});
    CHECK(std::get<ir::ConceptRef>(para[10]) == ir::ConceptRef{"c"});
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

TEST_CASE("draft_import - a bare \\ref, an exposition-only concept and a cv-qualifier placeholder are kinds") {
    // Each is the kind its nearest relative already imports to, with the one thing
    // that tells them apart: `\ref` is `\iref` without the parentheses, `\exposconcept`
    // a concept that is exposition-only, `\cvqual` a placeholder for cv-qualifiers.
    // None is reported, and the LaTeX backend writes each macro back as it was read.
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
Within subclause~\ref{y}, a type that satisfies \exposconcept{constexpr-param} for some \cvqual{cv2}.
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(r.diagnostics.empty());
    const auto& para = std::get<ir::BodyParagraph>(item_at(only_section(r), 0).descr.leading.front()).text;
    REQUIRE(para.size() == 7);
    CHECK(std::get<ir::RefInline>(para[1]) == ir::RefInline{.stable_name = "y", .bare = true});
    CHECK(std::get<ir::ConceptRef>(para[3]) == ir::ConceptRef{.name = "constexpr-param", .exposition = true});
    CHECK(std::get<ir::PlaceholderInline>(para[5]) == ir::PlaceholderInline{.text = "cv2", .cv_qualifier = true});
    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    CHECK(rendered.contains(
        "Within subclause~\\ref{y}, a type that satisfies \\exposconcept{constexpr-param} for some \\cvqual{cv2}."));
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
                                        std::pair{"func.wrap.ref.slice", "func.wrap.ref"},
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

TEST_CASE("draft_import - a placeholder in \\tcode keeps its \\tcode when written back") {
    // `\exposid` and `\seebelow` set the code font themselves, so the LaTeX backend
    // writes a code run that is one of them bare. `\placeholder` sets italic, and the
    // draft's `\tcode{\placeholder{x}}` is monospace italic: it is written as read.
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
Within, \tcode{\placeholder{call-args}} is a pack and \exposid{z} is not.
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    REQUIRE(r.diagnostics.empty());
    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    CHECK(rendered.contains("\\tcode{\\placeholder{call-args}} is a pack and \\exposid{z} is not"));
}

TEST_CASE("draft_import - \\cvqual and \\opt in code are spans, in a \\tcode and in a listing") {
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
Of the form \tcode{R(G::*)(A...) \cvqual{cv2} \opt{\&} noexcept(E)}, as in:
\begin{codeblock}
is_convertible_v<int @\cv{}@&, int @\cvqual{cv2}@&> @\opt{noexcept}@
\end{codeblock}
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(r.diagnostics.empty());
    const auto& leading = item_at(only_section(r), 0).descr.leading;
    const auto& code    = std::get<ir::CodeInline>(std::get<ir::BodyParagraph>(leading.at(0)).text.at(1)).code;
    CHECK(code.text == "R(G::*)(A...) cv2 & noexcept(E)");
    REQUIRE(code.spans.size() == 2);
    CHECK(code.spans[0] == ir::Span{14, 17, ir::SpanKind::CvQualifier, "cv2"});
    // The span covers the code that may be absent, with its escape resolved.
    CHECK(code.spans[1] == ir::Span{18, 19, ir::SpanKind::Optional, ""});
    const auto& listing = std::get<ir::AuthoredCode>(leading.at(1)).code;
    CHECK(listing.text == "is_convertible_v<int cv&, int cv2&> noexcept");
    REQUIRE(listing.spans.size() == 3);
    CHECK(listing.spans[1] == ir::Span{30, 33, ir::SpanKind::CvQualifier, "cv2"});
    CHECK(listing.spans[2] == ir::Span{36, 44, ir::SpanKind::Optional, ""});
    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    CHECK(rendered.contains("Of the form \\tcode{R(G::*)(A...) \\cvqual{cv2} \\opt{\\&} noexcept(E)}, as in:"));
    CHECK(rendered.contains("is_convertible_v<int @\\cv{}@&, int @\\cvqual{cv2}@&> @\\opt{noexcept}@\n"));
}

TEST_CASE("draft_import - a span says which form of its macro the draft wrote, where the context would not") {
    // The draft has a no-italic-correction twin of each italic macro and chooses by
    // hand. In a \tcode the plain form is what this tool writes, so the twin is
    // recorded; in a listing an exposition-only name is written as the twin, so there
    // it is the plain form that is recorded. Either way the wording comes back out as
    // the draft spells it (issue #306).
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
The type \tcode{R(*)(\exposidnc{B}, \exposid{c}) noexcept(\placeholdernc{noex})}, and \exposidnc{z}, \placeholdernc{p}, \exposconceptnc{k} and \seebelownc.
\begin{codeblock}
bool @\exposid{a}@ = @\exposidnc{b}@(@\placeholdernc{noex}@, @\placeholder{q}@) @\seebelownc@;
\end{codeblock}
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(r.diagnostics.empty());
    const auto& leading = item_at(only_section(r), 0).descr.leading;
    const auto& para    = std::get<ir::BodyParagraph>(leading.at(0)).text;
    const auto& code    = std::get<ir::CodeInline>(para.at(1)).code;
    REQUIRE(code.spans.size() == 3);
    CHECK(code.spans[0].no_correction == std::optional<bool>{true});
    CHECK(code.spans[1].no_correction == std::nullopt);
    CHECK(code.spans[2].no_correction == std::optional<bool>{true});
    CHECK(std::get<ir::CodeInline>(para.at(3)).code.spans.at(0).no_correction == std::optional<bool>{true});
    CHECK(std::get<ir::PlaceholderInline>(para.at(5)).no_correction);
    CHECK(std::get<ir::ConceptRef>(para.at(7)) ==
          ir::ConceptRef{.name = "k", .exposition = true, .no_correction = true});
    CHECK(std::get<ir::CodeInline>(para.at(9)).code.spans.at(0).no_correction == std::optional<bool>{true});
    const auto& listing = std::get<ir::AuthoredCode>(leading.at(1)).code;
    REQUIRE(listing.spans.size() == 5);
    CHECK(listing.spans[0].no_correction == std::optional<bool>{false});
    CHECK(listing.spans[1].no_correction == std::nullopt);
    CHECK(listing.spans[2].no_correction == std::optional<bool>{true});
    CHECK(listing.spans[3].no_correction == std::nullopt);
    CHECK(listing.spans[4].no_correction == std::optional<bool>{true});

    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    CHECK(
        rendered.contains("The type \\tcode{R(*)(\\exposidnc{B}, \\exposid{c}) noexcept(\\placeholdernc{noex})}, and "
                          "\\exposidnc{z}, \\placeholdernc{p}, \\exposconceptnc{k} and \\seebelownc."));
    CHECK(rendered.contains("bool @\\exposid{a}@ = @\\exposidnc{b}@(@\\placeholdernc{noex}@, @\\placeholder{q}@) "
                            "@\\seebelownc@;\n"));
}

TEST_CASE("draft_import - a line-break hint in code is kept beside the text, and written back where it was") {
    // The hint is not code, so the text and a span's name are clean; and it is not
    // dropped, so the LaTeX comes back as the draft has it (issue #306). The spaces
    // after `\newline` are TeX's to discard and are not code either.
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
With \tcode{std::forward<Args>\brk{}(args)...}, \tcode{ilist, std::for\-ward<Args>(args)...},
\tcode{remove_cvref_t<ArgTypes>::\newline
value...}, \exposid{bound-en\-tity}, \exposid{bound-\brk{}entity} and \tcode{x.\exposid{thunk-\brk ptr}}.
\begin{codeblock}
R (*@\exposid{thunk-\brk{}ptr}@)(int);
\end{codeblock}
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(r.diagnostics.empty());
    const auto& leading = item_at(only_section(r), 0).descr.leading;
    const auto& para    = std::get<ir::BodyParagraph>(leading.at(0)).text;
    const auto  code_at = [&](std::size_t i) -> const ir::CodeText& {
        return std::get<ir::CodeInline>(para.at(i)).code;
    };
    using Hints = std::vector<ir::LayoutHint>;
    CHECK(code_at(1).text == "std::forward<Args>(args)...");
    CHECK(code_at(1).hints == Hints{{18, ir::LayoutHintKind::Break, true}});
    CHECK(code_at(3).text == "ilist, std::forward<Args>(args)...");
    CHECK(code_at(3).hints == Hints{{15, ir::LayoutHintKind::Hyphen}});
    CHECK(code_at(5).text == "remove_cvref_t<ArgTypes>::value...");
    CHECK(code_at(5).hints == Hints{{26, ir::LayoutHintKind::NewLine}});
    CHECK(code_at(7).text == "bound-entity");
    CHECK(code_at(7).spans.at(0).payload == "bound-entity");
    CHECK(code_at(7).hints == Hints{{8, ir::LayoutHintKind::Hyphen}});
    CHECK(code_at(9).hints == Hints{{6, ir::LayoutHintKind::Break, true}});
    CHECK(code_at(11).text == "x.thunk-ptr");
    CHECK(code_at(11).spans.at(0) == ir::Span{2, 11, ir::SpanKind::ExposId, "thunk-ptr"});
    CHECK(code_at(11).hints == Hints{{8, ir::LayoutHintKind::Break}});
    const auto& listing = std::get<ir::AuthoredCode>(leading.at(1)).code;
    CHECK(listing.text == "R (*thunk-ptr)(int);");
    CHECK(listing.spans.at(0) == ir::Span{4, 13, ir::SpanKind::ExposId, "thunk-ptr", false});
    CHECK(listing.hints == Hints{{10, ir::LayoutHintKind::Break, true}});

    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    CHECK(rendered.contains(
        "With \\tcode{std::forward<Args>\\brk{}(args)...}, \\tcode{ilist, std::for\\-ward<Args>(args)...}, "
        "\\tcode{remove_cvref_t<ArgTypes>::\\newline value...}, \\exposid{bound-en\\-tity}, "
        "\\exposid{bound-\\brk{}entity} and \\tcode{x.\\exposid{thunk-\\brk ptr}}."));
    CHECK(rendered.contains("R (*@\\exposid{thunk-\\brk{}ptr}@)(int);\n"));
}

TEST_CASE("draft_import - a variable and a subscript written as mathematics in a \\tcode are spans") {
    // The draft writes an index into code as mathematics. It is not code: as
    // text the dollar signs were escaped and printed (issue #311).
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
The element is \tcode{std::forward<T$_i$>(get<$i$>(u))}, for \tcode{T$_{10}$} and \tcode{T$_{N}$}.
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(r.diagnostics.empty());
    const auto& para = std::get<ir::BodyParagraph>(item_at(only_section(r), 0).descr.leading.front()).text;
    const auto& code = std::get<ir::CodeInline>(para.at(1)).code;
    CHECK(code.text == "std::forward<T_i>(get<i>(u))");
    REQUIRE(code.spans.size() == 2);
    CHECK(code.spans[0] == ir::Span{14, 16, ir::SpanKind::Subscript, "i"});
    CHECK(code.spans[1] == ir::Span{22, 23, ir::SpanKind::MathVariable, "i"});
    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    // A subscript of one character comes back without its group.
    CHECK(rendered.contains(
        "The element is \\tcode{std::forward<T$_i$>(get<$i$>(u))}, for \\tcode{T$_{10}$} and \\tcode{T$_N$}."));
}

TEST_CASE("draft_import - other mathematics in a \\tcode keeps the \\tcode whole as raw LaTeX") {
    // An expression has no span kind. The run is kept as the LaTeX it was and
    // reported once, and the macros inside the mathematics are not read as code.
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
The range \tcode{a[$i - 1$]} through \tcode{a[$\dotsc$]}, and \tcode{b[$j$]}.
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    REQUIRE(r.diagnostics.size() == 1);
    CHECK(r.diagnostics.front().message == "mathematics in \\tcode is kept as raw LaTeX");
    const auto& para = std::get<ir::BodyParagraph>(item_at(only_section(r), 0).descr.leading.front()).text;
    REQUIRE(std::holds_alternative<ir::RawInline>(para.at(1)));
    CHECK(std::get<ir::RawInline>(para.at(1)).latex == "\\tcode{a[$i - 1$]}");
    CHECK(std::holds_alternative<ir::RawInline>(para.at(3)));
    CHECK(std::holds_alternative<ir::CodeInline>(para.at(5)));
    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    CHECK(rendered.contains("The range \\tcode{a[$i - 1$]} through \\tcode{a[$\\dotsc$]}, and \\tcode{b[$j$]}."));
}

TEST_CASE("draft_import - an \\opt whose argument holds a macro is not a span") {
    // A span cannot hold another span, so the format grammar's
    // `\opt{\fmtgrammarterm{arg-id}}` stays what it was: unread, and reported.
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
If \tcode{\{ \opt{\fmtgrammarterm{arg-id}} \}} is used.
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(has_diag(r, Severity::Warning, "kept as raw LaTeX: \\opt"));
    const auto& para = std::get<ir::BodyParagraph>(item_at(only_section(r), 0).descr.leading.front()).text;
    CHECK(std::holds_alternative<ir::RawInline>(para[1]));
}

TEST_CASE("draft_import - a \\tcode holding a macro the IR has no kind for is kept whole as raw LaTeX") {
    // Kept as the text `\grammarterm{id}`, the LaTeX backend would escape the backslash and
    // the braces and the wording would print the macro; kept as LaTeX it writes back as it was.
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
For \tcode{R(Args...) \grammarterm{id} noexcept}, and \tcode{plain}.
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    REQUIRE(r.diagnostics.size() == 1);
    CHECK(r.diagnostics.front().message.ends_with("kept as raw LaTeX: \\grammarterm"));
    const auto& para = std::get<ir::BodyParagraph>(item_at(only_section(r), 0).descr.leading.front()).text;
    REQUIRE(std::holds_alternative<ir::RawInline>(para[1]));
    CHECK(std::get<ir::RawInline>(para[1]).latex == "\\tcode{R(Args...) \\grammarterm{id} noexcept}");
    CHECK(std::holds_alternative<ir::CodeInline>(para[3]));
    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    CHECK(rendered.contains("For \\tcode{R(Args...) \\grammarterm{id} noexcept}, and \\tcode{plain}."));
}

TEST_CASE("draft_import - a comment ends its line, and the space before it is still a space") {
    // TeX discards a comment through the line end, so `text%` runs into the
    // next line and `text % note` does not; a line that is only a comment says
    // nothing either way (issue #313).
    constexpr std::string_view tex = R"(\rSec3[x]{X}

\pnum
The value is % Overfull
\tcode{true}, the name is \tcode{a}%
\tcode{b}, one
% a note between two lines
two, three%
% a note after a joined line
four.
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(r.diagnostics.empty());
    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    CHECK(rendered.find("The value is \\tcode{true}, the name is \\tcode{a}\\tcode{b}, one two, threefour.") !=
          std::string::npos);
}

TEST_CASE("draft_import - prose after a block that opens an element continues the element's paragraph") {
    // The block is the first thing after the tag, so it opens the numbered
    // paragraph, and what follows it is the rest of that paragraph (issue #315).
    constexpr std::string_view tex = R"(\rSec3[x]{X}

\begin{itemdecl}
void swap(optional& x);
\end{itemdecl}

\begin{itemdescr}
\pnum
\constraints
\begin{codeblock}
is_reference_v<T> || is_swappable_v<T>
\end{codeblock}
is \tcode{true}.

\pnum
\effects
\begin{itemize}
\item
If a, nothing.
\item
Otherwise, b.
\end{itemize}
Then the result is \tcode{x}.

\pnum
Finally, a paragraph with no tag.
\end{itemdescr}
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(r.diagnostics.empty());
    const auto& descr = item_at(only_section(r), 0).descr;
    REQUIRE(descr.elements.size() == 2);

    const auto& constraints = descr.elements[0].body;
    REQUIRE(constraints.size() == 2);
    CHECK(std::holds_alternative<ir::AuthoredCode>(constraints[0]));
    CHECK(std::get<ir::BodyParagraph>(constraints[1]).continuation);

    const auto& effects = descr.elements[1].body;
    REQUIRE(effects.size() == 3);
    CHECK(std::holds_alternative<ir::Itemize>(effects[0]));
    CHECK(std::get<ir::BodyParagraph>(effects[1]).continuation);
    // The untagged paragraph has a \pnum of its own, and keeps it.
    CHECK_FALSE(std::get<ir::BodyParagraph>(effects[2]).continuation);

    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    CHECK(std::ranges::distance(std::views::split(rendered, std::string_view{"\\pnum"})) == 4);
}
