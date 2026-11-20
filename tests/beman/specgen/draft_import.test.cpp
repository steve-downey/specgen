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

// A paragraph's words, its code inlines as their text: enough to say which
// paragraph it is.
std::string prose_of(const ir::Paragraph& p) {
    return p | std::views::transform([](const ir::Inline& piece) {
               if (const auto* text = std::get_if<ir::TextInline>(&piece))
                   return text->text;
               if (const auto* code = std::get_if<ir::CodeInline>(&piece))
                   return code->code.text;
               return std::string{};
           }) |
           std::views::join | std::ranges::to<std::string>();
}

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
    // The exposition-only comment reads as the words a generated synopsis has (issue #353).
    CHECK(sigs[0].text.ends_with("; // exposition only"));
    REQUIRE(sigs[1].spans.size() == 2);
    CHECK(sigs[1].spans[0].kind == ir::SpanKind::Placeholder);
    CHECK(sigs[1].spans[0].payload == "cv");
    CHECK(sigs[1].text.find("@\\added{x}@") != std::string::npos);
    CHECK(sigs[1].spans[1].kind == ir::SpanKind::Ref);
    CHECK(sigs[1].spans[1].payload == "y");
}

TEST_CASE("draft_import - an escaped ampersand, percent or hash in prose is the character") {
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
R\&D, 50\% of it, item \#1, and \{ as written.
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(r.diagnostics.empty());
    const auto& para = std::get<ir::BodyParagraph>(item_at(only_section(r), 0).descr.leading.at(0)).text;
    CHECK(std::get<ir::TextInline>(para.at(0)).text == "R&D, 50% of it, item #1, and \\{ as written.");
}

TEST_CASE("draft_import - only the macro \\expos in a listing is the words exposition only") {
    const auto code = draft_import::parse_listing("int a; // \\expos\nint b; // \\expos.\nint c; // \\exposition\n");
    CHECK(code.text == "int a; // exposition only\nint b; // exposition only.\nint c; // \\exposition\n");
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
A \textbf{thing} costs $O(n)$.
\begin{tokens}
foo
\end{tokens}
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(has_diag(r, Severity::Warning, "\\textbf has no IR kind"));
    CHECK(has_diag(r, Severity::Warning, "mathematics"));
    CHECK(has_diag(r, Severity::Warning, "tokens environment"));
    for (const auto& d : r.diagnostics)
        CHECK(d.section == "x");
    const auto& para = std::get<ir::BodyParagraph>(item_at(only_section(r), 0).descr.leading.front()).text;
    CHECK(std::holds_alternative<ir::RawInline>(para[1]));
    CHECK(std::get<ir::RawInline>(para[1]).latex == "\\textbf{thing}");
}

TEST_CASE("draft_import - a term and a fixed phrase are inline kinds, and come back as written") {
    // Issue #355, from [expected.bad] and [expected.object.general].
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
An \impldef{return value of \tcode{what}} \ntbs{}, a \term{valid value type for \tcode{expected}}, the \Fundescx{Effects}, \defn{a term}, \ntmbs, and \term{a \placeholder{p}}.
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    REQUIRE(r.diagnostics.size() == 1);
    CHECK(has_diag(r, Severity::Warning, "\\term holds what a term cannot"));
    const auto& para = std::get<ir::BodyParagraph>(item_at(only_section(r), 0).descr.leading.front()).text;
    CHECK(std::get<ir::PhraseInline>(para.at(1)) ==
          ir::PhraseInline{ir::Phrase::ImplementationDefined, "return value of \\tcode{what}"});
    CHECK(std::get<ir::PhraseInline>(para.at(3)).phrase == ir::Phrase::Ntbs);
    const auto& term = std::get<ir::TermInline>(para.at(5));
    REQUIRE(term.content.size() == 2);
    CHECK(std::get<ir::TextInline>(term.content[0]).text == "valid value type for ");
    CHECK(std::get<ir::CodeInline>(term.content[1]).code.text == "expected");
    CHECK(std::get<ir::TermInline>(para.at(7)).role == ir::TermRole::ElementName);
    CHECK(std::get<ir::TermInline>(para.at(9)).role == ir::TermRole::Definition);
    CHECK(std::get<ir::PhraseInline>(para.at(11)).phrase == ir::Phrase::Ntmbs);

    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    CHECK(rendered.contains("An \\impldef{return value of \\tcode{what}} \\ntbs{}, a \\term{valid value type for "
                            "\\tcode{expected}}, the \\Fundescx{Effects}, \\defn{a term}, \\ntmbs, and "
                            "\\term{a \\placeholder{p}}."));
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
    // hand. The plain form is what this tool writes, in a \tcode and in a listing
    // alike (issue #341), so the twin is what is recorded, and the wording comes back
    // out as the draft spells it (issue #306).
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
    CHECK(listing.spans[0].no_correction == std::nullopt);
    CHECK(listing.spans[1].no_correction == std::optional<bool>{true});
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
    CHECK(listing.spans.at(0) == ir::Span{4, 13, ir::SpanKind::ExposId, "thunk-ptr"});
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

TEST_CASE("draft_import - other mathematics in a \\tcode is a span holding its LaTeX") {
    // An expression or a Greek letter has no kind of its own (issue #339). It
    // is a `Math` span holding the LaTeX between the dollars, so the code
    // around it is still code, and the LaTeX backend writes it back as it was.
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
The range \tcode{a[$i - 1$]} through \tcode{a[$\rho$]}, and \tcode{b[$j$]}.
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(r.diagnostics.empty());
    const auto& para = std::get<ir::BodyParagraph>(item_at(only_section(r), 0).descr.leading.front()).text;
    const auto& expr = std::get<ir::CodeInline>(para.at(1)).code;
    CHECK(expr.text == "a[i - 1]");
    CHECK(expr.spans == std::vector<ir::Span>{{2, 7, ir::SpanKind::Math, "i - 1"}});
    const auto& greek = std::get<ir::CodeInline>(para.at(3)).code;
    CHECK(greek.spans == std::vector<ir::Span>{{2, 6, ir::SpanKind::Math, "\\rho"}});
    CHECK(std::holds_alternative<ir::CodeInline>(para.at(5)));
    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    CHECK(rendered.contains("The range \\tcode{a[$i - 1$]} through \\tcode{a[$\\rho$]}, and \\tcode{b[$j$]}."));
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

TEST_CASE("draft_import - a line-break hint in running text and in a prose inline's name is kept beside the text") {
    // In running text a hint used to be dropped, and in the argument of a prose
    // inline it stayed in the name, which the other backends printed (issue #314).
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
Meets the \oldconcept{Unary\-Type\-Trait} requirements, is \tcode{true}, \linebreak \tcode{false} otherwise,
models \libconcept{in\-vocable}, for \placeholder{long-\brk{}name}, imple\-men\-ta\-tion,
and \tcode{a}\brk{}\tcode{b}, wanted\linebreak[2] or not.
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(r.diagnostics.empty());
    const auto& para = std::get<ir::BodyParagraph>(item_at(only_section(r), 0).descr.leading.front()).text;
    using Hints      = std::vector<ir::LayoutHint>;

    const auto& old = std::get<ir::OldConceptRef>(para.at(1));
    CHECK(old.name == "UnaryTypeTrait");
    CHECK(old.hints == Hints{{5, ir::LayoutHintKind::Hyphen}, {9, ir::LayoutHintKind::Hyphen}});
    // The hint between two code inlines sits at the end of the text before it;
    // the spaces after the control word are TeX's to discard.
    const auto& between = std::get<ir::TextInline>(para.at(4));
    CHECK(between.text == ", ");
    CHECK(between.hints == Hints{{2, ir::LayoutHintKind::LineBreak}});
    const auto& concept_ref = std::get<ir::ConceptRef>(para.at(7));
    CHECK(concept_ref.name == "invocable");
    CHECK(concept_ref.hints == Hints{{2, ir::LayoutHintKind::Hyphen}});
    const auto& placeholder = std::get<ir::PlaceholderInline>(para.at(9));
    CHECK(placeholder.text == "long-name");
    CHECK(placeholder.hints == Hints{{5, ir::LayoutHintKind::Break, true}});
    const auto& word = std::get<ir::TextInline>(para.at(10));
    CHECK(word.text == ", implementation, and ");
    CHECK(word.hints ==
          Hints{{7, ir::LayoutHintKind::Hyphen}, {10, ir::LayoutHintKind::Hyphen}, {12, ir::LayoutHintKind::Hyphen}});
    // A hint with no text beside it is a text inline that holds it alone.
    const auto& alone = std::get<ir::TextInline>(para.at(12));
    CHECK(alone.text.empty());
    CHECK(alone.hints == Hints{{0, ir::LayoutHintKind::Break, true}});
    // \linebreak[2] has no kind: it is dropped, with its bracket.
    CHECK(std::get<ir::TextInline>(para.at(14)).text == ", wanted or not.");

    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    CHECK(rendered.contains(
        "Meets the \\oldconcept{Unary\\-Type\\-Trait} requirements, is \\tcode{true}, \\linebreak \\tcode{false} "
        "otherwise, models \\libconcept{in\\-vocable}, for \\placeholder{long-\\brk{}name}, imple\\-men\\-ta\\-tion, "
        "and \\tcode{a}\\brk{}\\tcode{b}, wanted or not."));
}

TEST_CASE("draft_import - the draft's tie is a space the line may not break at, in prose and in a \\tcode") {
    // `~` is a tie, not a tilde: read as the character it printed a tilde in a
    // \tcode, and in prose in every backend but LaTeX (issue #312). A tilde in
    // a \tcode is `\~`, and in a listing the character is the code's own.
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
Within subclause~\ref{x}, \tcode{T}~is \tcode{R(*)(G, A...)~\opt{noexcept}} and \tcode{val.T::\~T()}.
\begin{codeblock}
~T();
\end{codeblock}
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(r.diagnostics.empty());
    const auto& leading = item_at(only_section(r), 0).descr.leading;
    const auto& para    = std::get<ir::BodyParagraph>(leading.at(0)).text;
    using Hints         = std::vector<ir::LayoutHint>;
    CHECK(std::get<ir::TextInline>(para.at(0)) ==
          ir::TextInline{"Within subclause ", {{16, ir::LayoutHintKind::Tie}}});
    CHECK(std::get<ir::TextInline>(para.at(4)) == ir::TextInline{" is ", {{0, ir::LayoutHintKind::Tie}}});
    const auto& code = std::get<ir::CodeInline>(para.at(5)).code;
    CHECK(code.text == "R(*)(G, A...) noexcept");
    CHECK(code.hints == Hints{{13, ir::LayoutHintKind::Tie}});
    const auto& tilde = std::get<ir::CodeInline>(para.at(7)).code;
    CHECK(tilde.text == "val.T::~T()");
    CHECK(tilde.hints.empty());
    const auto& listing = std::get<ir::AuthoredCode>(leading.at(1)).code;
    CHECK(listing.text == "~T();");
    CHECK(listing.hints.empty());

    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    CHECK(rendered.contains("Within subclause~\\ref{x}, \\tcode{T}~is \\tcode{R(*)(G, A...)~\\opt{noexcept}} and "
                            "\\tcode{val.T::\\textasciitilde{}T()}."));
    CHECK(rendered.contains("\n~T();\n"));
}

TEST_CASE("draft_import - a note, an example, a code block or a list with its own \\pnum keeps the number") {
    // [tuple.elem] numbers the note after *Returns* by itself; so does much of
    // the library (issue #321).
    constexpr std::string_view tex = R"(\rSec3[x]{X}

\begin{itemdecl}
void f();
\end{itemdecl}

\begin{itemdescr}
\pnum
\returns
A reference to the element.
\begin{note}
In the paragraph before it.
\end{note}

\pnum
\begin{note}
Numbered by itself.
\end{note}
And the rest of that paragraph.

\pnum
\begin{example}
\begin{codeblock}
f();
\end{codeblock}
\end{example}

\pnum
\begin{codeblock}
g();
\end{codeblock}

\pnum
\begin{itemize}
\item One.
\item Two.
\end{itemize}

\pnum
\remarks
\begin{note}
First in its body.
\end{note}
\end{itemdescr}
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(r.diagnostics.empty());
    const auto& descr = item_at(only_section(r), 0).descr;
    REQUIRE(descr.elements.size() == 2);

    const auto& returns = descr.elements[0].body;
    REQUIRE(returns.size() == 7);
    CHECK_FALSE(std::get<ir::Note>(returns[1]).opens_paragraph);
    CHECK(std::get<ir::Note>(returns[2]).opens_paragraph);
    // The number is the note's, so the prose after it continues.
    CHECK(std::get<ir::BodyParagraph>(returns[3]).continuation);
    CHECK(std::get<ir::Example>(returns[4]).opens_paragraph);
    CHECK(std::get<ir::AuthoredCode>(returns[5]).opens_paragraph);
    CHECK(std::get<ir::Itemize>(returns[6]).opens_paragraph);

    // The first block of a body opens its paragraph without saying so, as
    // generated wording has it.
    const auto& remarks = descr.elements[1].body;
    REQUIRE(remarks.size() == 1);
    CHECK_FALSE(std::get<ir::Note>(remarks[0]).opens_paragraph);

    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    CHECK(std::ranges::distance(std::views::split(rendered, std::string_view{"\\pnum"})) == 7);
    CHECK(rendered.contains("\\pnum\n\\begin{note}\nNumbered by itself.\n\\end{note}\nAnd the rest"));
    CHECK(rendered.contains("\\end{note}\n\n\\pnum\n\\begin{note}"));
}

TEST_CASE("draft_import - a subscript on code or on a variable, and an ellipsis, in a \\tcode are spans") {
    // [tuple] is written in terms of `$\tcode{T}_i$`, which says what `T$_i$`
    // says (issue #327). The text is the same; a hint keeps the draft's choice
    // of where the mathematics opens, so it is written back as it was.
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
For \tcode{tuple<$\tcode{T}_i$...>} and \tcode{tuple<T$_i$...>}, the types \tcode{T$_0$, $\dotsc$, $\tcode{T}_{N}$}
and the value \tcode{$r_K$}.
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(r.diagnostics.empty());
    const auto& para = std::get<ir::BodyParagraph>(item_at(only_section(r), 0).descr.leading.front()).text;

    const auto& inside  = std::get<ir::CodeInline>(para.at(1)).code;
    const auto& outside = std::get<ir::CodeInline>(para.at(3)).code;
    CHECK(inside.text == "tuple<T_i...>");
    CHECK(inside.spans == std::vector{ir::Span{7, 9, ir::SpanKind::Subscript, "i"}});
    CHECK(inside.hints == std::vector{ir::LayoutHint{6, ir::LayoutHintKind::MathOpen, false}});
    // The two spellings are the same wording, and differ in typesetting alone.
    CHECK(outside.hints.empty());
    CHECK(ir::without_typesetting(inside) == ir::without_typesetting(outside));

    const auto& sequence = std::get<ir::CodeInline>(para.at(5)).code;
    CHECK(sequence.text == "T_0, ..., T_N");
    REQUIRE(sequence.spans.size() == 3);
    CHECK(sequence.spans[1] == ir::Span{5, 8, ir::SpanKind::Ellipsis, ""});
    // The group around a subscript of one character is the draft's spelling, and is kept.
    CHECK(sequence.hints == std::vector{ir::LayoutHint{10, ir::LayoutHintKind::MathOpen, true}});

    const auto& variable = std::get<ir::CodeInline>(para.at(7)).code;
    CHECK(variable.text == "r_K");
    CHECK(variable.spans ==
          std::vector{ir::Span{0, 1, ir::SpanKind::MathVariable, "r"}, ir::Span{1, 3, ir::SpanKind::Subscript, "K"}});

    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    CHECK(rendered.contains("For \\tcode{tuple<$\\tcode{T}_i$...>} and \\tcode{tuple<T$_i$...>}, the types "
                            "\\tcode{T$_0$, $\\dotsc$, $\\tcode{T}_{N}$} and the value \\tcode{$r_K$}."));
}

TEST_CASE("draft_import - mathematics in a \\tcode that is not one of the shapes read is a math span") {
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
\tcode{a$\tcode{T}$} \tcode{a$\tcode{\exposid{t}}_i$} \tcode{a$x_{i*1}$} \tcode{a$\ldots$} \tcode{a$_$} \tcode{a$1_i$}
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(r.diagnostics.empty());
    const auto& para = std::get<ir::BodyParagraph>(item_at(only_section(r), 0).descr.leading.front()).text;
    const auto  math =
        para | std::views::filter([](const ir::Inline& i) { return std::holds_alternative<ir::CodeInline>(i); }) |
        std::views::transform([](const ir::Inline& i) {
            const auto& code = std::get<ir::CodeInline>(i).code;
            return code.spans.size() == 1 && code.spans.front().kind == ir::SpanKind::Math ? code.spans.front().payload
                                                                                           : std::string{};
        }) |
        std::ranges::to<std::vector>();
    CHECK(math == std::vector<std::string>{"\\tcode{T}", "\\tcode{\\exposid{t}}_i", "x_{i*1}", "\\ldots", "_", "1_i"});
    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    CHECK(rendered.contains(R"(\tcode{a$\tcode{T}$} \tcode{a$\tcode{\exposid{t}}_i$} \tcode{a$x_{i*1}$} )"
                            R"(\tcode{a$\ldots$} \tcode{a$_$} \tcode{a$1_i$})"));
}

TEST_CASE("draft_import - an unclosed dollar in a \\tcode keeps the \\tcode whole as raw LaTeX") {
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
The value \tcode{a$b}.
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    REQUIRE(r.diagnostics.size() == 1);
    CHECK(r.diagnostics.front().message == "mathematics in \\tcode is kept as raw LaTeX");
    const auto& para = std::get<ir::BodyParagraph>(item_at(only_section(r), 0).descr.leading.front()).text;
    CHECK(std::holds_alternative<ir::RawInline>(para.at(1)));
}

TEST_CASE("draft_import - a placeholder with a subscript, and code as the subscript of code") {
    // Issue #339: [alg.req.ind.cmp]'s `$\tcode{\placeholder{it}}_i$` and
    // [exec.snd.expos]'s `$\tcode{Ds}_{\tcode{T}}$`.
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
\tcode{f($\tcode{\placeholder{it}}_i$)}, \tcode{$\placeholder{C}_i$}, and \tcode{$\tcode{Ds}_{\tcode{T}}$::x}.
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(r.diagnostics.empty());
    const auto& para  = std::get<ir::BodyParagraph>(item_at(only_section(r), 0).descr.leading.front()).text;
    const auto& coded = std::get<ir::CodeInline>(para.at(0)).code;
    CHECK(coded.text == "f(it_i)");
    CHECK(coded.spans ==
          std::vector<ir::Span>{{2, 4, ir::SpanKind::Placeholder, "it"}, {4, 6, ir::SpanKind::Subscript, "i"}});
    CHECK(coded.hints == std::vector<ir::LayoutHint>{{2, ir::LayoutHintKind::MathOpen, false}});
    const auto& bare = std::get<ir::CodeInline>(para.at(2)).code;
    CHECK(bare.hints.empty());
    const auto& code_sub = std::get<ir::CodeInline>(para.at(4)).code;
    CHECK(code_sub.text == "Ds_T::x");
    CHECK(code_sub.spans == std::vector<ir::Span>{{2, 4, ir::SpanKind::CodeSubscript, "T"}});
    CHECK(code_sub.hints == std::vector<ir::LayoutHint>{{0, ir::LayoutHintKind::MathOpen, true}});
    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    CHECK(rendered.contains(R"(\tcode{f($\tcode{\placeholder{it}}_i$)}, \tcode{$\placeholder{C}_i$}, and )"
                            R"(\tcode{$\tcode{Ds}_{\tcode{T}}$::x}.)"));
}

TEST_CASE("draft_import - mathematics in running text that reads as code is a code inline marked as mathematics") {
    // Issue #339: `$\tcode{T}_i$`, a lone variable, a variable and its
    // subscript, and an ordinal, in running text. Anything else stays raw.
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
For $\tcode{T}_i$ and each $i$, $r_K$, the $i^\text{th}$ and $\tcode{I}^\text{nd}$ ones in $[0, N)$.
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    REQUIRE(r.diagnostics.size() == 1);
    CHECK(r.diagnostics.front().message == "mathematics is kept as raw LaTeX");
    const auto& para = std::get<ir::BodyParagraph>(item_at(only_section(r), 0).descr.leading.front()).text;
    const auto  math = [&](std::size_t at) {
        const auto& inline_ = std::get<ir::CodeInline>(para.at(at));
        CHECK(inline_.mathematics);
        return inline_.code;
    };
    CHECK(math(1).text == "T_i");
    CHECK(math(1).hints == std::vector<ir::LayoutHint>{{0, ir::LayoutHintKind::MathOpen, false}});
    CHECK(math(3).spans == std::vector<ir::Span>{{0, 1, ir::SpanKind::MathVariable, "i"}});
    CHECK(math(5).text == "r_K");
    CHECK(math(7).text == "ith");
    CHECK(math(7).spans ==
          std::vector<ir::Span>{{0, 1, ir::SpanKind::MathVariable, "i"}, {1, 3, ir::SpanKind::Ordinal, "th"}});
    CHECK(math(9).text == "Ind");
    CHECK(std::holds_alternative<ir::RawInline>(para.at(11)));
    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    CHECK(rendered.contains(R"(For $\tcode{T}_i$ and each $i$, $r_K$, the $i^\text{th}$ and $\tcode{I}^\text{nd}$ )"
                            R"(ones in $[0, N)$.)"));
}

TEST_CASE("draft_import - code and an expression as a subscript in a \\tcode are spans") {
    // Issue #339: [range.zip.transform.iterator]'s `$i_\tcode{x}$`, and
    // [inplace.vector]'s `$p_{i-1}$`. Code as the subscript of code is read too.
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
\tcode{$i_\tcode{x}$ + 1}, \tcode{a[$p_{i-1}$]}, \tcode{$elems_{n+1}$}, and \tcode{$\tcode{Ds}_{\tcode{T}}$}.
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(r.diagnostics.empty());
    const auto& para = std::get<ir::BodyParagraph>(item_at(only_section(r), 0).descr.leading.front()).text;
    const auto& sub  = std::get<ir::CodeInline>(para.at(0)).code;
    CHECK(sub.text == "i_x + 1");
    REQUIRE(sub.spans.size() == 2);
    CHECK(sub.spans[1] == ir::Span{1, 3, ir::SpanKind::CodeSubscript, "x"});
    const auto& expr = std::get<ir::CodeInline>(para.at(2)).code;
    CHECK(expr.text == "a[p_i-1]");
    CHECK(expr.spans.at(1) == ir::Span{3, 7, ir::SpanKind::Subscript, "i-1"});
    CHECK(std::get<ir::CodeInline>(para.at(4)).code.spans.at(1).payload == "n+1");
    CHECK(std::get<ir::CodeInline>(para.at(6)).code.spans.at(0).kind == ir::SpanKind::CodeSubscript);

    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    CHECK(rendered.contains(
        R"(\tcode{$i_\tcode{x}$ + 1}, \tcode{a[$p_{i-1}$]}, \tcode{$elems_{n+1}$}, and \tcode{$\tcode{Ds}_{\tcode{T}}$}.)"));
}

TEST_CASE("draft_import - a listing records where each of its bytes was read from") {
    constexpr std::string_view tex = R"(\rSec3[x]{X}

\begin{codeblock}
class c {
  R f() @\cv{}@;
};
\end{codeblock}

\begin{itemdecl}
template<class T>
  R g(T) @\cv{}@;
\end{itemdecl}
)";
    const auto                 r   = import_sections(tex, Options{{"x"}, "x.tex"});
    CHECK(r.diagnostics.empty());
    const auto& section = only_section(r);
    const auto  check   = [&](const ir::CodeText& code) {
        REQUIRE(code.origin.source != nullptr);
        const ir::CodeSource& source = *code.origin.source;
        CHECK(source.file == "x.tex");
        REQUIRE(source.bytes.size() == code.text.size());
        // substrate generic algorithm: every byte, against the text it came from.
        for (std::size_t i = 0; i < code.text.size(); ++i) {
            const auto [begin, end]     = source.bytes[i];
            const std::string_view from = tex.substr(begin, end - begin);
            const auto             span =
                std::ranges::find_if(code.spans, [&](const ir::Span& s) { return s.begin <= i && i < s.end; });
            if (span == code.spans.end())
                CHECK(from == code.text.substr(i, 1));
            else
                CHECK(from == "@\\cv{}@"); // a span's bytes stand for its whole escape
        }
    };
    check(std::get<ir::Synopsis>(section.children.at(0)).code);
    check(item_at(section, 1).decl.signatures.front());
    // Provenance is not wording: the same code read from elsewhere is equal.
    ir::CodeText bare = item_at(section, 1).decl.signatures.front();
    bare.origin       = {};
    CHECK(bare == item_at(section, 1).decl.signatures.front());
}

TEST_CASE("draft_import - a list item holding a code block is the item's paragraph and the code after it") {
    // Issue #367: [expected.object.assign]'s shape, and prose after the code
    // inside the same item.
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
\effects
\begin{itemize}
\item
If \tcode{a}, equivalent to:
\begin{codeblock}
f();
\end{codeblock}
and then done.
\item
Otherwise, no effect.
\end{itemize}
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(r.diagnostics.empty());
    const auto& item = item_at(only_section(r), 0);
    const auto& list = std::get<ir::Itemize>(item.descr.elements.front().body.front());
    REQUIRE(list.items.size() == 2);
    CHECK(prose_of(list.items[0].text) == "If a, equivalent to:");
    REQUIRE(list.items[0].blocks.size() == 2);
    CHECK(std::get<ir::AuthoredCode>(list.items[0].blocks[0]).code.text == "f();");
    CHECK(prose_of(std::get<ir::BodyParagraph>(list.items[0].blocks[1]).text) == "and then done.");
    CHECK(list.items[1].blocks.empty());
    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    CHECK(rendered.contains("\\item If \\tcode{a}, equivalent to:\n\\begin{codeblock}\nf();\n\\end{codeblock}\n"
                            "and then done.\n\\item Otherwise, no effect.\n"));
}

TEST_CASE("draft_import - a list nested in a list item is one of the item's blocks") {
    // Issue #381: [res.on.data.races]'s shape, a nested list after the
    // item's text, and prose after the nested list; the nested items hold
    // their own code block and their own nested list. The nested list's
    // `\item`s do not end the outer item.
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
\effects
\begin{itemize}
\item
One of:
\begin{itemize}
\item
\tcode{a}, equivalent to:
\begin{codeblock}
f();
\end{codeblock}
\item
\tcode{b}, and then:
\begin{itemize}
\item \tcode{c}
\end{itemize}
\end{itemize}
whichever comes first.
\item
Otherwise, no effect.
\end{itemize}
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    CHECK(r.diagnostics.empty());
    const auto& item = item_at(only_section(r), 0);
    const auto& list = std::get<ir::Itemize>(item.descr.elements.front().body.front());
    REQUIRE(list.items.size() == 2);
    CHECK(prose_of(list.items[0].text) == "One of:");
    REQUIRE(list.items[0].blocks.size() == 2);
    const auto& inner = std::get<ir::Itemize>(list.items[0].blocks[0]);
    CHECK_FALSE(inner.opens_paragraph);
    REQUIRE(inner.items.size() == 2);
    CHECK(prose_of(inner.items[0].text) == "a, equivalent to:");
    REQUIRE(inner.items[0].blocks.size() == 1);
    CHECK(std::get<ir::AuthoredCode>(inner.items[0].blocks[0]).code.text == "f();");
    REQUIRE(inner.items[1].blocks.size() == 1);
    const auto& innermost = std::get<ir::Itemize>(inner.items[1].blocks[0]);
    REQUIRE(innermost.items.size() == 1);
    CHECK(prose_of(innermost.items[0].text) == "c");
    CHECK(prose_of(std::get<ir::BodyParagraph>(list.items[0].blocks[1]).text) == "whichever comes first.");
    CHECK(prose_of(list.items[1].text) == "Otherwise, no effect.");
    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    CHECK(rendered.contains("\\item One of:\n\\begin{itemize}\n\\item \\tcode{a}, equivalent to:\n"
                            "\\begin{codeblock}\nf();\n\\end{codeblock}\n\\item \\tcode{b}, and then:\n"
                            "\\begin{itemize}\n\\item \\tcode{c}\n\\end{itemize}\n\\end{itemize}\n"
                            "whichever comes first.\n\\item Otherwise, no effect.\n"));
}

TEST_CASE("draft_import - a list item holding a note, an example or a numbered list is kept as raw LaTeX") {
    // Issue #381: none is one of an informative body's blocks.
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
\begin{itemize}
\item
One.
\begin{note}
Two.
\end{note}
\end{itemize}

\pnum
\begin{itemize}
\item
One:
\begin{enumerate}
\item two
\end{enumerate}
\end{itemize}

\pnum
\begin{itemize}
\item
One.
\begin{example}
Two.
\end{example}
\end{itemize}
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    REQUIRE(r.diagnostics.size() == 3);
    CHECK(r.diagnostics[0].message == "a list item holding a note is kept as raw LaTeX");
    CHECK(r.diagnostics[1].message == "a nested numbered list is kept as raw LaTeX");
    CHECK(r.diagnostics[2].message == "a list item holding an example is kept as raw LaTeX");
}

TEST_CASE("draft_import - a floattable in the shape of a lib2dtab2 is a table, and keeps its form") {
    // Issue #367: [expected.object.swap]'s table. One of any other shape stays raw.
    constexpr std::string_view tex = R"(\rSec3[x]{X}
\pnum
See \tref{x.t}.

\begin{floattable}{\tcode{f} effects}{x.t}
{lx{0.35\hsize}x{0.35\hsize}}
\topline
& \chdr{\tcode{a}} & \rhdr{\tcode{!a}} \\ \capsep
\lhdr{\tcode{b}} &
  one &
  two \\
\lhdr{\tcode{!b}} &
  three &
  four \\
\end{floattable}

\begin{floattable}{Other}{x.u}
{ll}
\topline
\lhdr{A} & \rhdr{B} \\ \capsep
a & b \\
\end{floattable}
)";
    const auto                 r   = import_sections(tex, Options{{"x"}});
    REQUIRE(r.diagnostics.size() == 1);
    CHECK(r.diagnostics.front().message == "the floattable environment is kept as raw LaTeX");
    const auto& leading = item_at(only_section(r), 0).descr.leading;
    const auto  table   = std::ranges::find_if(
        leading, [](const ir::BodyBlock& block) { return std::holds_alternative<ir::Table2D>(block); });
    REQUIRE(table != leading.end());
    const auto& t = std::get<ir::Table2D>(*table);
    CHECK(t.stable_name == "x.t");
    CHECK(t.floattable_columns == "lx{0.35\\hsize}x{0.35\\hsize}");
    REQUIRE(t.rows.size() == 2);
    CHECK(prose_of(t.rows[1].header) == "!b");
    CHECK(prose_of(t.rows[1].cell2) == "four");
    const std::string rendered = backend::latex::render_to_string(r.document, {.base_section_depth = 3});
    CHECK(
        rendered.contains("\\begin{floattable}{\\tcode{f} effects}{x.t}\n{lx{0.35\\hsize}x{0.35\\hsize}}\n\\topline\n"
                          "& \\chdr{\\tcode{a}} & \\rhdr{\\tcode{!a}} \\\\ \\capsep\n"
                          "\\lhdr{\\tcode{b}} &\n  one &\n  two \\\\\n"));
}
