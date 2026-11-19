// include/beman/specgen/backend/draft_spans.hpp                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — the working draft's own LaTeX spelling of a span and of a code
// inline, shared by the two backends that write draft LaTeX: latex, and org
// inside its code blocks and export snippets.
#ifndef BEMAN_SPECGEN_BACKEND_DRAFT_SPANS_HPP
#define BEMAN_SPECGEN_BACKEND_DRAFT_SPANS_HPP

#include <beman/specgen/backend/code_spans.hpp>
#include <beman/specgen/foundation/enumeration.hpp>
#include <beman/specgen/ir.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <functional>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace beman::specgen::backend {

// --- the draft's own LaTeX spelling of a span ------------------------------
//
// The mirror image of `element_label` (element_label.hpp), and shared for the
// mirror-image reason. That table exists because a backend that is *not* writing draft
// LaTeX has to supply the label the macro would have carried; these exist
// because a backend that *is* writing draft LaTeX has to spell the macros
// exactly as the draft does, and there are two of those.
//
// The org backend is the second, which is not obvious and is the whole
// justification for hoisting these out of `latex.cpp`: its code blocks are
// `#+begin_codeblock`/`#+begin_itemdecl` special blocks, which org's stock
// `org-latex-special-block` exports to `\begin{codeblock}`/`\begin{itemdecl}`
// -- the draft's own `lstnewenvironment`s, reaching a wg21org paper through
// `common.tex`'s `\input{stdtex/macros}`. So the bytes inside an org code
// block and inside a draft codeblock are the same bytes, produced against the
// same `escapechar=@` and `texcl=true`. A divergence between the two backends
// here would be a defect rather than a difference; copying the switch would
// have made one possible.
//
// Not `escape_span_*`, which is what `latex.cpp` called them, because the
// name now has to say *whose* convention it is rather than what it does to a
// string.

// Literal code inside a text-mode macro argument (issue #248). The span
// walker applies this only to source slices, before inserting draft macros.
// Listings payloads and author-supplied @...@ escapes use the identity hook.
// The draft loads the underscore package: `_` stays literal (issue #252).
inline std::string draft_escape_code_text(std::string_view text) {
    return text | std::views::transform([](char ch) -> std::string {
               switch (ch) {
               case '&':
                   return "\\&";
               case '#':
                   return "\\#";
               case '%':
                   return "\\%";
               case '{':
                   return "\\{";
               case '}':
                   return "\\}";
               case '$':
                   return "\\$";
               case '~':
                   return "\\textasciitilde{}";
               case '^':
                   return "\\textasciicircum{}";
               case '\\':
                   return "\\textbackslash{}";
               default:
                   return std::string(1, ch);
               }
           }) |
           std::views::join | std::ranges::to<std::string>();
}

// A span's macro, the same in prose and inside a listing's escape. The draft
// has a no-italic-correction twin of each italic macro; a span the draft
// importer read says which one the draft wrote, and one that does not say
// takes the plain form (`ir::Span::no_correction`).
inline std::string draft_span_macro(const ir::Span& span, std::string_view spelling) {
    const bool nc = span.no_correction.value_or(false);
    return foundation::match(
        span.kind,
        foundation::when<ir::SpanKind::ExposId>(
            [&] { return (nc ? "\\exposidnc{" : "\\exposid{") + span.payload + '}'; }),
        foundation::when<ir::SpanKind::SeeBelow>([&] { return std::string{nc ? "\\seebelownc" : "\\seebelow"}; }),
        foundation::when<ir::SpanKind::ImplDefined>([&] { return std::string{nc ? "\\impdefnc" : "\\impdef"}; }),
        // \cv{} is the one placeholder the draft gives its own macro, which the
        // draft's listings and its prose both use.
        foundation::when<ir::SpanKind::Placeholder>([&] {
            if (span.payload == "cv")
                return std::string{"\\cv{}"};
            return (nc ? "\\placeholdernc{" : "\\placeholder{") + span.payload + '}';
        }),
        // `\ref`, not `\iref`. A Ref *span* is a cross-reference
        // inside a code comment, which the draft writes bare --
        // `// \ref{optional.ctor}, constructors` -- where `\iref` is the
        // parenthesized form (`\nolinebreak\hspace{1sp}(\ref{...})`) that
        // belongs in prose. `ir::RefInline` is the prose one and
        // renders `\iref`; the two are different cross-references and
        // must not share a mapping.
        foundation::when<ir::SpanKind::Ref>([&] { return "\\ref{" + span.payload + '}'; }),
        foundation::when<ir::SpanKind::LibraryIndex>([&] {
            if (span.payload.empty())
                return std::format("\\libglobal{{{}}}", spelling);
            return std::format("\\libmember{{{}}}{{{}}}", spelling, span.payload);
        }),
        foundation::when<ir::SpanKind::CvQualifier>([&] { return "\\cvqual{" + span.payload + '}'; }),
        // The argument is the covered code, and a macro argument is text mode
        // wherever the macro sits -- in a \tcode and inside a listing's
        // `@...@` alike -- so it takes the text-mode escapes either way.
        foundation::when<ir::SpanKind::Optional>([&] { return "\\opt{" + draft_escape_code_text(spelling) + '}'; }),
        // Mathematics inside code, as the draft writes it: `get<$i$>(u)` and
        // `T$_i$`. A subscript of one character needs no group.
        foundation::when<ir::SpanKind::MathVariable>([&] { return '$' + span.payload + '$'; }),
        foundation::when<ir::SpanKind::Subscript>(
            [&] { return span.payload.size() == 1 ? "$_" + span.payload + '$' : "$_{" + span.payload + "}$"; }),
        // The elements left out of a sequence: `T$_0$, $\dotsc$, T$_n$`.
        foundation::when<ir::SpanKind::Ellipsis>([&] { return std::string{"$\\dotsc$"}; }),
        // Code as a subscript, in the code font inside the mathematics:
        // `$i_\tcode{x}$` (issue #339).
        foundation::when<ir::SpanKind::CodeSubscript>(
            [&] { return "$_\\tcode{" + draft_escape_code_text(span.payload) + "}$"; }),
        // Mathematics with no other kind is the draft's own LaTeX, written
        // back as it was: `$\rho$` (issue #339).
        foundation::when<ir::SpanKind::Math>([&] { return '$' + span.payload + '$'; }),
        // An ordinal suffix, `$i^\text{th}$`: the variable or code before it
        // opens the mathematics, and this closes it.
        foundation::when<ir::SpanKind::Ordinal>([&] { return "$^\\text{" + span.payload + "}$"; }));
}

// Outside a codeblock we are already in LaTeX, so the macro is written
// directly.
inline std::string draft_span_prose(const ir::Span& span, std::string_view spelling) {
    return draft_span_macro(span, spelling);
}

// Inside a codeblock the draft escapes back into LaTeX with @...@. Ref is the
// exception,
// and it is the draft's own, not ours. `macros.tex` sets the
// listings option `texcl=true` ("all TeX commands are available within
// comments") in the shared `\lstset`, alongside `escapechar=@` -- so it holds
// for `itemdecl` as much as for `codeblock`, and a `//` comment in either is
// *already* in TeX mode: the draft writes `// \ref{optional.ctor}` and
// `// \tcode{optional}` bare there, and reserves `@...@` for macros in code
// proper (`@\impdefnc@`). A Ref span only ever sits in a comment -- that is
// what the enumerator means -- so it is the one kind that must not be
// wrapped; wrapping it would typeset the delimiters.
//
// `draft_span_escaped` is the form *inside* an open `@...@`: what the
// codeblock form wraps, which is also what a span inside a changed run of
// code renders as, the run having opened the escape already
// (docs/plans/wording-diff.md). A Ref is the bare `\ref` either way.
inline std::string draft_span_escaped(const ir::Span& span, std::string_view spelling) {
    return draft_span_macro(span, spelling);
}

inline std::string draft_span_codeblock(const ir::Span& span, std::string_view spelling) {
    if (span.kind == ir::SpanKind::Ref)
        return draft_span_prose(span, spelling);
    return '@' + draft_span_escaped(span, spelling) + '@';
}

// A listing's own text, as the draft's source writes it. The draft writes the
// exposition-only comment as its macro, `// \expos` (`\EXPO{exposition only}`
// in macros.tex), and never as the words (issue #353); the importer reads the
// macro as the words, so the IR has one spelling of it, and this writes it
// back. A comment in a listing is LaTeX (texcl), so the macro is written bare.
inline std::string draft_listing_text(std::string_view text) {
    constexpr std::string_view words = "// exposition only";
    std::string                out;
    std::size_t                pos = 0;
    // substrate generic algorithm: a find-and-replace scan; the position is
    // the loop's state.
    for (std::size_t at = text.find(words); at != std::string_view::npos; at = text.find(words, pos)) {
        out += text.substr(pos, at - pos);
        out += "// \\expos";
        pos = at + words.size();
    }
    out += text.substr(pos);
    return out;
}

// --- the draft's own change marks (macros.tex) ------------------------------
//
// `\added{...}` and `\removed{...}` in prose; the same inside a code escape,
// which is where a changed run of code goes; and the `addedblock` /
// `removedblock` environments around a whole node. Shared with the org
// backend for the reason the span spellings are: inside its code blocks the
// bytes are the draft's, and its `#+begin_addedblock` is the special block
// that exports to the environment of the same name.

inline std::string draft_change_prose(ir::Change change, std::string_view text) {
    return foundation::match(
        change,
        foundation::when<ir::Change::Same>([&] { return std::string(text); }),
        foundation::when<ir::Change::Added>([&] { return "\\added{" + std::string(text) + '}'; }),
        foundation::when<ir::Change::Removed>([&] { return "\\removed{" + std::string(text) + '}'; }),
        foundation::when<ir::Change::Elided>([&] { return std::string(text); }));
}

inline std::string draft_change_codeblock(ir::Change change, std::string_view text) {
    if (change == ir::Change::Same)
        return std::string(text);
    return '@' + draft_change_prose(change, text) + '@';
}

// The environment a marked node sits in, and nothing for an unmarked one.
inline std::optional<std::string_view> draft_change_environment(ir::Change change) {
    return foundation::match(
        change,
        foundation::when<ir::Change::Same>([] { return std::optional<std::string_view>{}; }),
        foundation::when<ir::Change::Added>([] { return std::optional<std::string_view>{"addedblock"}; }),
        foundation::when<ir::Change::Removed>([] { return std::optional<std::string_view>{"removedblock"}; }),
        foundation::when<ir::Change::Elided>([] { return std::optional<std::string_view>{}; }));
}

// One of the draft's line-break hints, as the draft writes it: the macro, and
// the empty group after it when the source had one; or `~` for a tie. A control word with no
// group must not run into a letter, so it takes the space TeX would discard;
// `following` is the text after the hint.
inline std::string draft_layout_hint_macro(const ir::LayoutHint& hint, std::string_view following) {
    std::string macro =
        foundation::match(hint.kind,
                          foundation::when<ir::LayoutHintKind::Break>([] { return std::string{"\\brk"}; }),
                          foundation::when<ir::LayoutHintKind::Hyphen>([] { return std::string{"\\-"}; }),
                          foundation::when<ir::LayoutHintKind::LineBreak>([] { return std::string{"\\linebreak"}; }),
                          foundation::when<ir::LayoutHintKind::NewLine>([] { return std::string{"\\newline"}; }),
                          foundation::when<ir::LayoutHintKind::Tie>([] { return std::string{"~"}; }),
                          // Not a macro at a point: it opens mathematics that a
                          // subscript closes, and only `draft_code_inline`, which
                          // sees both ends, writes it.
                          foundation::when<ir::LayoutHintKind::MathOpen>([] { return std::string{}; }));
    // A tie is a character, not a control word: nothing follows it.
    if (hint.kind == ir::LayoutHintKind::Tie || hint.kind == ir::LayoutHintKind::MathOpen)
        return macro;
    if (hint.braced)
        return macro + "{}";
    const bool letter_follows = !following.empty() && ((following.front() >= 'a' && following.front() <= 'z') ||
                                                       (following.front() >= 'A' && following.front() <= 'Z'));
    if (hint.kind != ir::LayoutHintKind::Hyphen && letter_follows)
        macro += ' ';
    return macro;
}

// Prose text as LaTeX: the three characters LaTeX reserves that prose has as
// themselves -- `&` (an alignment tab), `%` (a comment, which dropped the rest
// of a paragraph written on one line) and `#` -- written as the draft writes
// them, `\&`, `\%` and `\#` (issue #352). The rest stays as it is: prose keeps
// a command the grammar does not know as written (design §4), so a backslash or
// a brace in it is the author's LaTeX.
inline std::string draft_escape_prose_text(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    // substrate generic algorithm: a per-character escape, one character to
    // one or two.
    for (const char ch : text) {
        if (ch == '&' || ch == '%' || ch == '#')
            out += '\\';
        out += ch;
    }
    return out;
}

// Text with its line-break hints written where they sit: a text inline's
// running text, or the name in a prose inline's argument. The text between
// hints goes through `escape_text`, the hints do not. A control word that ends
// the text takes a space as one before a letter does: what follows is another
// inline's macro, and the draft mostly writes the space.
template <typename EscapeText = std::identity>
std::string
draft_hinted_text(std::string_view text, const std::vector<ir::LayoutHint>& hints, EscapeText&& escape_text = {}) {
    std::string out;
    std::size_t pos = 0;
    // substrate generic algorithm: the position-tracking walk of
    // code_spans.hpp, over a hint table with no spans.
    for (const ir::LayoutHint& hint : hints) {
        if (hint.at < pos || hint.at > text.size())
            continue; // defensive: out of order, or past the end
        out += escape_text(text.substr(pos, hint.at - pos));
        out += draft_layout_hint_macro(hint, text.substr(hint.at));
        const bool control_word = hint.kind != ir::LayoutHintKind::Hyphen && hint.kind != ir::LayoutHintKind::Tie &&
                                  hint.kind != ir::LayoutHintKind::MathOpen;
        if (!hint.braced && control_word && hint.at == text.size())
            out += ' ';
        // A tie stands in place of the space it is on.
        pos = hint.at + (ir::tied_space(hint, text) ? 1 : 0);
    }
    out += escape_text(text.substr(pos));
    return out;
}

// The hint writer for code in prose, a \tcode: the text is LaTeX, so a hint is
// its macro wherever it sits.
inline std::string draft_layout_hint(const ir::LayoutHint& hint, std::string_view following, bool /*in_span*/) {
    return draft_layout_hint_macro(hint, following);
}

// The hint writer for a listing. A hint inside a span's payload is inside the
// span's `@...@` escape and is its macro. One in the listing's own text is
// not written: that text is verbatim, a listing breaks its lines where its
// text does, and the draft has no hint there for the importer to have read.
inline std::string draft_layout_hint_listing(const ir::LayoutHint& hint, std::string_view following, bool in_span) {
    return in_span ? draft_layout_hint_macro(hint, following) : std::string{};
}

// A placeholder name in prose, as the draft writes one: \cv{} for the one
// placeholder the draft gives its own macro (which also sets right in math),
// \cvqual for any other that stands for cv-qualifiers, \placeholder for the
// rest. Shared because the org backend writes the same text in its LaTeX
// export snippet.
inline std::string draft_placeholder(const ir::PlaceholderInline& v) {
    if (v.text == "cv" && !v.cv_qualifier)
        return "\\cv{}";
    const std::string name = draft_hinted_text(v.text, v.hints);
    if (v.cv_qualifier)
        return "\\cvqual{" + name + '}';
    return (v.no_correction ? "\\placeholdernc{" : "\\placeholder{") + name + '}';
}

// A concept name in prose: \libconcept, or \exposconcept for an
// exposition-only one.
inline std::string draft_concept(const ir::ConceptRef& v) {
    const std::string name = draft_hinted_text(v.name, v.hints);
    if (!v.exposition)
        return "\\libconcept{" + name + '}';
    return (v.no_correction ? "\\exposconceptnc{" : "\\exposconcept{") + name + '}';
}

// A C++17 named requirement in prose: \oldconcept, which prints the Cpp17
// prefix itself.
inline std::string draft_old_concept(const ir::OldConceptRef& v) {
    return "\\oldconcept{" + draft_hinted_text(v.name, v.hints) + '}';
}

// A code inline, in prose, as the draft writes one. Shared for the same
// reason the two above are: the org backend emits this exact text inside an
// `@@latex:...@@` export snippet, because org's own `~code~` markup is
// verbatim and cannot carry a span.
//
// A code inline that is *entirely* one span renders as the bare macro: the
// draft's \exposid/\seebelow already set the code font, so wrapping them in
// \tcode would double up. Partial spans still need the wrapper
// (`x.\exposid{val}`).
inline std::string draft_code_inline(const ir::CodeInline& v) {
    // A placeholder is the exception: `\placeholder` sets italic, not the code
    // font, and the draft writes `\tcode{\placeholder{x}}` for it. `\cvqual`
    // is the same italic, `\opt` sets its argument in whatever font
    // surrounds it, and mathematics is mathematics, so those keep the wrapper
    // as well.
    const auto sets_code_font = [](ir::SpanKind kind) {
        return kind != ir::SpanKind::Placeholder && kind != ir::SpanKind::CvQualifier &&
               kind != ir::SpanKind::Optional && kind != ir::SpanKind::MathVariable &&
               kind != ir::SpanKind::Subscript && kind != ir::SpanKind::Ellipsis &&
               kind != ir::SpanKind::CodeSubscript && kind != ir::SpanKind::Math && kind != ir::SpanKind::Ordinal;
    };
    const bool whole_span = v.code.spans.size() == 1 && v.code.spans.front().begin == 0 &&
                            v.code.spans.front().end == v.code.text.size() &&
                            sets_code_font(v.code.spans.front().kind);
    // A subscript and what it is on are one run of mathematics, as the draft
    // writes them: `$r_K$` for a variable and its subscript, where a span each
    // would give `$r$$_K$`, and `$\tcode{T}_i$` where a `MathOpen` hint says
    // the draft put the code inside the mathematics too. What is open, if
    // anything, when the walk reaches a subscript.
    //
    // An ordinal suffix is the same as a subscript here (`$i^\text{th}$`), and
    // a placeholder can be what a subscript is on (`$\placeholder{C}_i$`, or
    // `$\tcode{\placeholder{it}}_i$` with a `MathOpen` hint where it begins).
    enum class Open { Nothing, Variable, Code };
    Open       open      = Open::Nothing;
    bool       grouped   = false; // the draft wrote the subscript in a group
    const auto is_suffix = [](ir::SpanKind kind) {
        return kind == ir::SpanKind::Subscript || kind == ir::SpanKind::CodeSubscript || kind == ir::SpanKind::Ordinal;
    };
    const auto subscript_begins_at = [&](std::size_t at) {
        return std::ranges::any_of(v.code.spans,
                                   [&](const ir::Span& span) { return span.begin == at && is_suffix(span.kind); });
    };
    const auto math_opens_at = [&](std::size_t at) {
        return std::ranges::any_of(v.code.hints, [&](const ir::LayoutHint& hint) {
            return hint.at == at && hint.kind == ir::LayoutHintKind::MathOpen;
        });
    };
    const auto write_span = [&](const ir::Span& span, std::string_view spelling) {
        std::string macro = draft_span_prose(span, spelling);
        if (is_suffix(span.kind) && open != Open::Nothing) {
            // The mathematics is open: no `$` of its own, and the code's group closes first.
            if (open == Open::Code && grouped && span.kind == ir::SpanKind::Subscript && span.payload.size() == 1)
                macro = "}_{" + span.payload + "}$";
            else if (open == Open::Code && grouped && span.kind == ir::SpanKind::CodeSubscript)
                macro = "}_{\\tcode{" + draft_escape_code_text(span.payload) + "}}$";
            else
                macro = (open == Open::Code ? "}" : "") + macro.substr(1);
            open = Open::Nothing;
        } else if ((span.kind == ir::SpanKind::MathVariable || span.kind == ir::SpanKind::Placeholder) &&
                   subscript_begins_at(span.end)) {
            const bool code = span.kind == ir::SpanKind::Placeholder && math_opens_at(span.begin);
            if (span.kind == ir::SpanKind::Placeholder)
                macro = (code ? "$\\tcode{" : "$") + macro + '$';
            macro.pop_back(); // the subscript that follows closes it
            open = code ? Open::Code : Open::Variable;
            if (code)
                grouped = std::ranges::any_of(v.code.hints, [&](const ir::LayoutHint& hint) {
                    return hint.at == span.begin && hint.kind == ir::LayoutHintKind::MathOpen && hint.braced;
                });
            return macro;
        }
        // A suffix followed by another, `$r_K^\text{th}$`, leaves it open.
        if (is_suffix(span.kind) && subscript_begins_at(span.end)) {
            macro.pop_back();
            open = Open::Variable;
        }
        return macro;
    };
    // `following` runs to the next span, so a hint with a subscript at its end
    // has the whole of that subscript's base after it. Any other is left
    // alone, and the subscript is then written on its own.
    const auto write_hint = [&](const ir::LayoutHint& hint, std::string_view following, bool in_span) {
        if (hint.kind != ir::LayoutHintKind::MathOpen)
            return draft_layout_hint(hint, following, in_span);
        if (in_span || following.empty() || !subscript_begins_at(hint.at + following.size()))
            return std::string{};
        open    = Open::Code;
        grouped = hint.braced;
        return std::string{"$\\tcode{"};
    };
    // A prose code inline takes no change mark of its own (the mark is on the
    // inline), so the run hooks are the plain ones. The walk is taken for a
    // whole-span inline too: it is what writes a line-break hint inside the
    // span's name, and around one span it yields the bare macro.
    const std::string code = render_code_spans(
        v.code,
        write_span,
        write_span,
        [](ir::Change, std::string_view text) { return std::string(text); },
        draft_escape_code_text,
        write_hint);
    return whole_span || v.mathematics ? code : "\\tcode{" + code + '}';
}

// A term in prose (issue #355): the draft's macro for its role, around its
// words and code as prose writes them. Shared with the org backend for the
// reason the code inline is.
inline std::string draft_term(const ir::TermInline& v) {
    const std::string_view macro =
        foundation::match(v.role,
                          foundation::when<ir::TermRole::Term>([] { return std::string_view{"\\term{"}; }),
                          foundation::when<ir::TermRole::Definition>([] { return std::string_view{"\\defn{"}; }),
                          foundation::when<ir::TermRole::ElementName>([] { return std::string_view{"\\Fundescx{"}; }));
    std::string out{macro};
    std::ranges::for_each(v.content, [&](const ir::TermPiece& piece) {
        if (const auto* text = std::get_if<ir::TextInline>(&piece))
            out += draft_hinted_text(text->text, text->hints, draft_escape_prose_text);
        else
            out += draft_code_inline(std::get<ir::CodeInline>(piece));
    });
    return out + '}';
}

// A fixed phrase in prose as the draft writes it (issue #355). `\ntbs` takes
// an empty group, the draft's own spelling before a space, unless the draft
// wrote it bare.
inline std::string draft_phrase(const ir::PhraseInline& v) {
    return foundation::match(
        v.phrase,
        foundation::when<ir::Phrase::ImplementationDefined>([&] { return "\\impldef{" + v.index + '}'; }),
        foundation::when<ir::Phrase::Ntbs>([&] { return std::string{v.bare ? "\\ntbs" : "\\ntbs{}"}; }),
        foundation::when<ir::Phrase::Ntmbs>([&] { return std::string{v.bare ? "\\ntmbs" : "\\ntmbs{}"}; }));
}

} // namespace beman::specgen::backend

#endif // BEMAN_SPECGEN_BACKEND_DRAFT_SPANS_HPP
