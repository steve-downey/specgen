// include/beman/specgen/ir.hpp                                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — semantic IR for standard-library description wording.
// Node vocabulary is the wording ontology of [structure.specifications];
// no typography, no paragraph numbers, no backend escapes.
// See docs/architecture.md §7.

#ifndef BEMAN_SPECGEN_IR_HPP
#define BEMAN_SPECGEN_IR_HPP

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <beman/specgen/foundation/enumeration.hpp>

namespace beman::specgen::ir {

// --- code with semantic spans ---------------------------------------------
// Rendered code is never a flat string: byte-ranged spans carry semantics
// that each backend serializes with its own escape convention.
enum class SpanKind {
    ExposId,       // payload = kebab-case exposition-only name
    SeeBelow,      // "see below" substitution
    ImplDefined,   // "implementation-defined" substitution
    Placeholder,   // payload = placeholder text
    Ref,           // payload = stable name (cross-reference inside a comment)
    LibraryIndex,  // payload = enclosing class, or empty for a global name
    CvQualifier,   // payload = a placeholder that stands for cv-qualifiers (the draft's \cvqual)
    Optional,      // the covered text may be absent (the draft's \opt): it prints with a subscript "opt"
    MathVariable,  // payload = a variable of the surrounding mathematics, in math italic (the draft's $i$ in code)
    Subscript,     // payload = a subscript on what precedes it (the draft's $_i$ in code); the text is "_" + payload
    Ellipsis,      // the elements left out of a sequence (the draft's $\dotsc$ in code); the text is "..."
    CodeSubscript, // payload = code subscripting the variable before it (the draft's $i_\tcode{x}$); the text is "_" +
                   // payload
};

} // namespace beman::specgen::ir

template <>
inline constexpr auto beman::specgen::foundation::enumerators<beman::specgen::ir::SpanKind> = std::array{
    beman::specgen::ir::SpanKind::ExposId,
    beman::specgen::ir::SpanKind::SeeBelow,
    beman::specgen::ir::SpanKind::ImplDefined,
    beman::specgen::ir::SpanKind::Placeholder,
    beman::specgen::ir::SpanKind::Ref,
    beman::specgen::ir::SpanKind::LibraryIndex,
    beman::specgen::ir::SpanKind::CvQualifier,
    beman::specgen::ir::SpanKind::Optional,
    beman::specgen::ir::SpanKind::MathVariable,
    beman::specgen::ir::SpanKind::Subscript,
    beman::specgen::ir::SpanKind::Ellipsis,
    beman::specgen::ir::SpanKind::CodeSubscript,
};

namespace beman::specgen::ir {

std::string_view        span_kind_name(SpanKind); // "exposid", ...
std::optional<SpanKind> span_kind_from_name(std::string_view);

// --- change marks (docs/plans/wording-diff.md) -----------------------------
// What a wording diff says about a piece of the document: unchanged, added by
// the revision being rendered, or removed by it. A mark is an editing
// instruction (mpark's `::: add`, the draft's `\added`), semantic to the
// paper rather than typography, which is why it lives in the IR and not in a
// backend. A mark on a node covers everything beneath it, and the descendants
// of a marked node carry `Same`; marks never nest.
//
// `Same` sorts first so that a default-constructed node, and a hand-written
// golden that omits the key, is unmarked -- the JSON schema omits the key at
// this value, which is what keeps every pre-diff golden byte-identical.
//
// `Elided` is the fourth answer the `items` context gives: unchanged, and not
// shown. The differ puts it on one FreeParagraph standing in for a run of
// unchanged children inside a changed section, and a backend renders a node
// so marked as its ellipsis line, whatever the node holds.
enum class Change { Same, Added, Removed, Elided };

} // namespace beman::specgen::ir

template <>
inline constexpr auto beman::specgen::foundation::enumerators<beman::specgen::ir::Change> = std::array{
    beman::specgen::ir::Change::Same,
    beman::specgen::ir::Change::Added,
    beman::specgen::ir::Change::Removed,
    beman::specgen::ir::Change::Elided,
};

namespace beman::specgen::ir {

std::string_view      change_name(Change); // "same", "added", "removed", "elided"
std::optional<Change> change_from_name(std::string_view);

// Every IR struct compares structurally (defaulted `==`): the wording diff
// asks whether two pieces of two documents are the same piece, and a test
// asks whether a round trip gave back what it was handed.
struct Span {
    std::size_t begin = 0; // byte offsets into CodeText::text, [begin, end)
    std::size_t end   = 0;
    SpanKind    kind  = SpanKind::ExposId;
    std::string payload;
    // Typesetting, not wording: which of the draft's two forms of this span's
    // macro is written. Each italic macro has a twin that leaves out the
    // italic correction (\exposidnc, \placeholdernc, \seebelownc,
    // \impdefnc), and the draft's source picks one by hand, by what follows
    // the name. Unset is the plain form, which is what every generated span
    // has, in prose and in a listing alike: the draft's source writes
    // `@\exposid{...}@` in a listing thirteen times for each `@\exposidnc{...}@`
    // (issue #341). The draft importer sets it where the draft wrote the
    // no-correction form, so that inherited wording comes back out as the draft
    // spells it, and a generated declaration or synopsis takes it from the
    // baseline one it pairs with (inherit.hpp). Only a backend that writes
    // draft LaTeX reads it, and the wording diff compares without it.
    std::optional<bool> no_correction = {};

    friend bool operator==(const Span&, const Span&) = default;
};

// Whether a span is wording. A library index span is an index entry around a
// name, `\libglobal{x}` printing `x`: the draft writes it on some heads and not
// others, and a name is the same name with or without its entry, so the
// wording diff and `--apply-to-draft` read it as the name alone (issue #354).
constexpr bool is_wording(const Span& span) { return span.kind != SpanKind::LibraryIndex; }

// A run of changed bytes in a CodeText: a second table beside `spans` rather
// than two more `SpanKind`s, because a change run *contains* semantic spans
// whole and spans do not nest. Sorted by begin, non-overlapping, and never
// crossing a line end: the draft's `@...@` escape, which a backend wraps a run
// in, is line-local. A whole added line is one run.
struct ChangeRun {
    std::size_t begin  = 0;
    std::size_t end    = 0;
    Change      change = Change::Added;

    friend bool operator==(const ChangeRun&, const ChangeRun&) = default;
};

// A line-break hint in the draft's source: a place inside code where LaTeX may
// break the line, or must. The draft writes them by hand where a long name
// would overfill a line, and like `Span::no_correction` they are typesetting
// that inherited wording has to carry to come back out as the draft spells it.
enum class LayoutHintKind {
    Break,     // \brk: the line may break here, with no hyphen
    Hyphen,    // \-: the line may break here, with a hyphen
    LineBreak, // \linebreak: break here if it helps
    NewLine,   // \newline: break here
    Tie,       // ~: the space at this offset is one the line may not break at
    MathOpen,  // $\tcode{: the code from here to the subscript after it is inside that subscript's mathematics
};

} // namespace beman::specgen::ir

template <>
inline constexpr auto beman::specgen::foundation::enumerators<beman::specgen::ir::LayoutHintKind> = std::array{
    beman::specgen::ir::LayoutHintKind::Break,
    beman::specgen::ir::LayoutHintKind::Hyphen,
    beman::specgen::ir::LayoutHintKind::LineBreak,
    beman::specgen::ir::LayoutHintKind::NewLine,
    beman::specgen::ir::LayoutHintKind::Tie,
    beman::specgen::ir::LayoutHintKind::MathOpen,
};

namespace beman::specgen::ir {

std::string_view layout_hint_name(LayoutHintKind); // "brk", "hyphen", "linebreak", "newline", "tie", "math-open"
std::optional<LayoutHintKind> layout_hint_from_name(std::string_view);

// One hint, between two bytes of a text -- a CodeText's, a TextInline's, or the
// name of a prose inline: `at` is the offset of the
// byte it precedes, or the text's size for one at the end. `braced` says the
// draft wrote the empty group after the macro (`\brk{}`), which it mostly
// does and which is the only other thing there is to its spelling.
//
// A third table beside `spans` and `changes`, for the reason `changes` is a
// second one: a hint can sit inside a span (`\exposid{bound-en\-tity}`), and
// spans do not nest. Keeping the hints out of `text` and out of a span's
// payload is what lets everything that reads code -- the differ, the
// validator, the other two backends -- go on reading the name and not the
// LaTeX. Sorted by `at`. Only a backend that writes draft LaTeX reads them.
//
// A tie is the one hint that stands for a byte and does not sit between two:
// the draft's `~` is a space, so the text has a space at `at`, and the hint
// says the line may not break there. A writer that writes the tie writes it in
// place of that space (`tied_space`); one that does not leaves the space. It
// is also the one hint with a meaning outside LaTeX, and the other backends
// write a tie in running text as their own no-break space.
//
// `MathOpen` is not about line breaking at all, and is here because it is
// typesetting of the same standing: a spelling the draft chose that changes
// nothing a reader sees. A subscript on code is written `T$_i$` or
// `$\tcode{T}_i$`, and they print alike. Both are the text `T_i` with a
// `Subscript` span on `_i`; the second has this hint where the `T` begins, and
// a writer of draft LaTeX opens the mathematics there (issue #327). Its
// `braced` is the other thing there is to that spelling: the draft wrote a
// subscript of one character in a group, `$\tcode{B}_{1}$`.
struct LayoutHint {
    std::size_t    at     = 0;
    LayoutHintKind kind   = LayoutHintKind::Break;
    bool           braced = false;

    friend bool operator==(const LayoutHint&, const LayoutHint&) = default;
};

// Whether `hint` is a tie on a space of `text`: the byte a writer of the tie
// writes it in place of.
constexpr bool tied_space(const LayoutHint& hint, std::string_view text) {
    return hint.kind == LayoutHintKind::Tie && hint.at < text.size() && text[hint.at] == ' ';
}

// Where a code text was read from: the draft source file, and for each byte
// of the text the range of that file it stands for. A byte copied from the
// source stands for itself; the bytes of a span read from an `@...@` escape all
// stand for the whole escape. Only the draft importer records it, for code
// read from a listing, and only the output that edits the draft's source
// (draft_edit.hpp) reads it.
struct CodeSource {
    struct Range {
        std::size_t begin = 0;
        std::size_t end   = 0;

        friend bool operator==(const Range&, const Range&) = default;
    };
    std::string        file;
    std::vector<Range> bytes; // one per byte of the text

    friend bool operator==(const CodeSource&, const CodeSource&) = default;
};

// A code text's provenance, beside it and not part of it: two texts read from
// different places are the same wording, so it compares equal to every other,
// and the JSON form does not have it. Shared, since the text is copied often
// and the record never changes once read.
struct Provenance {
    std::shared_ptr<const CodeSource> source = {};

    friend bool operator==(const Provenance&, const Provenance&) { return true; }
};

// Where a node of a document was read from, the same way: bytes [begin, end)
// of `file`, whole lines, beside the node and not part of it (issue #346).
// Only the draft importer records it, and only draft_edit.hpp reads it.
struct SourceRange {
    std::string file;
    std::size_t begin = 0;
    std::size_t end   = 0;
};
struct NodeOrigin {
    std::shared_ptr<const SourceRange> range = {};

    friend bool operator==(const NodeOrigin&, const NodeOrigin&) { return true; }
};

struct CodeText {
    std::string             text;
    std::vector<Span>       spans;        // sorted by begin, non-overlapping
    std::vector<ChangeRun>  changes = {}; // the wording diff's marks, usually none
    std::vector<LayoutHint> hints   = {}; // the draft's line-break hints, usually none
    Provenance              origin  = {}; // where an imported listing was read from, usually nothing

    friend bool operator==(const CodeText&, const CodeText&) = default;
};

// --- prose -----------------------------------------------------------------
// Running text. `hints` are the draft's line-break hints in it, as
// `CodeText::hints` are for code: offsets into `text`, sorted, typesetting and
// not wording, and usually none. A hint that sits between two other inlines
// has no text to sit in, and is a text inline with a hint and no text.
struct TextInline {
    std::string             text;
    std::vector<LayoutHint> hints = {};

    friend bool operator==(const TextInline&, const TextInline&) = default;
};

// The bytes [begin, end) of a text inline, with the hints that sit in them: a
// hint precedes the byte at its offset, so one at `end` belongs to the piece
// that follows, unless `end` is the end of the text.
TextInline text_piece(const TextInline& text, std::size_t begin, std::size_t end);
// `more` joined onto the end of `text`, its hints moved along.
void append_text(TextInline& text, const TextInline& more);
struct CodeInline {
    CodeText code;

    friend bool operator==(const CodeInline&, const CodeInline&) = default;
}; // \tcode; spans allow exposid etc.
// A cross-reference to a stable name. The draft has two forms, and which one
// is written is part of the wording: \iref{x} is the parenthesized reference
// that annotates what precedes it ("the requirements (22.10.17)"), and
// \ref{x} is the number alone, which the sentence around it reads as a noun
// ("Within subclause 22.10.17, ..."). `bare` is the second.
struct RefInline {
    std::string stable_name;
    bool        bare = false;

    friend bool operator==(const RefInline&, const RefInline&) = default;
};
// A library concept name, rendered with the draft's \libconcept macro (not
// \tcode). Constraints prose uses the "`X` models `C`" form, where `C` is a
// ConceptRef; see the Constraints derivation (design §5.1).
//
// `exposition` marks an exposition-only concept, the draft's \exposconcept{x}:
// a name the wording defines for its own use, set in italics as an
// exposition-only identifier is and indexed apart from the library's concepts.
struct ConceptRef {
    std::string name;
    bool        exposition = false;
    // The draft wrote \exposconceptnc: typesetting, as `Span::no_correction` is.
    bool no_correction = false;
    // The draft's line-break hints inside the name (`\libconcept{con\-cept}`):
    // offsets into `name`, which is the name without them.
    std::vector<LayoutHint> hints = {};

    friend bool operator==(const ConceptRef&, const ConceptRef&) = default;
};
// An italic placeholder name: the draft's \placeholder{x}, and \cv{} (text
// "cv"), which sets the same italic in text and in math. Not code: the name
// stands for whatever the surrounding wording replaces it with.
//
// `cv_qualifier` marks a placeholder that stands for cv-qualifiers, the
// draft's \cvqual{x} (`cv2`, say). It prints as any placeholder does, and the
// LaTeX backend writes the macro the draft uses. \cv{} is \cvqual{cv}; it
// stays the text "cv" with this unset, as it was before the flag existed.
struct PlaceholderInline {
    std::string text;
    bool        cv_qualifier = false;
    // The draft wrote \placeholdernc: typesetting, as `Span::no_correction` is.
    bool no_correction = false;
    // The draft's line-break hints inside the name: offsets into `text`.
    std::vector<LayoutHint> hints = {};

    friend bool operator==(const PlaceholderInline&, const PlaceholderInline&) = default;
};
// A named requirement from the C++17 library tables, the draft's
// \oldconcept{X}, which prints "Cpp17X" in italics. `name` is the X.
struct OldConceptRef {
    std::string name;
    // The draft's line-break hints inside the name
    // (`\oldconcept{Unary\-Type\-Trait}`): offsets into `name`.
    std::vector<LayoutHint> hints = {};

    friend bool operator==(const OldConceptRef&, const OldConceptRef&) = default;
};
// A reference to a table by its stable name: the draft's \tref{x}, which
// prints "Table N" and links to the table's caption. A table's stable name is
// the one its `libtab2` or `lib2dtab2` carries.
struct TableRef {
    std::string stable_name;

    friend bool operator==(const TableRef&, const TableRef&) = default;
};
// Draft LaTeX the importer has no inline kind for, kept as written. It is the
// one place LaTeX survives into a backend that does not write LaTeX, and the
// importer reports a Warning for each, so nothing degrades silently.
struct RawInline {
    std::string latex;

    friend bool operator==(const RawInline&, const RawInline&) = default;
};
// An italic term of the wording (issue #355): the draft's \term{x}, a term
// used apart from its definition; \defn{x}, a term defined where it stands;
// and \Fundescx{X}, an element named in running text ("the expressions in the
// *Effects*"). The words may hold code, as `\term{valid value type for
// \tcode{expected}}` does, so they are text and code pieces.
enum class TermRole { Term, Definition, ElementName };
using TermPiece = std::variant<TextInline, CodeInline>;
struct TermInline {
    std::vector<TermPiece> content;
    TermRole               role = TermRole::Term;

    friend bool operator==(const TermInline&, const TermInline&) = default;
};

// A phrase the draft writes with a macro of its own (issue #355):
// \impldef{index} prints "implementation-defined" and indexes `index`, which
// is LaTeX for the index and never printed; \ntbs and \ntmbs print "NTBS" and
// "NTMBS" in small capitals.
enum class Phrase { ImplementationDefined, Ntbs, Ntmbs };
struct PhraseInline {
    Phrase      phrase = Phrase::ImplementationDefined;
    std::string index  = {};
    // Typesetting, as `Span::no_correction` is: the draft wrote `\ntbs` with
    // no empty group, which it does before punctuation. Unset is `\ntbs{}`.
    bool bare = false;

    friend bool operator==(const PhraseInline&, const PhraseInline&) = default;
};

} // namespace beman::specgen::ir

template <>
inline constexpr auto beman::specgen::foundation::enumerators<beman::specgen::ir::TermRole> = std::array{
    beman::specgen::ir::TermRole::Term,
    beman::specgen::ir::TermRole::Definition,
    beman::specgen::ir::TermRole::ElementName,
};

template <>
inline constexpr auto beman::specgen::foundation::enumerators<beman::specgen::ir::Phrase> = std::array{
    beman::specgen::ir::Phrase::ImplementationDefined,
    beman::specgen::ir::Phrase::Ntbs,
    beman::specgen::ir::Phrase::Ntmbs,
};

namespace beman::specgen::ir {

std::string_view        term_role_name(TermRole); // "term", "definition", "element-name"
std::optional<TermRole> term_role_from_name(std::string_view);
std::string_view        phrase_name(Phrase); // "implementation-defined", "ntbs", "ntmbs"
std::optional<Phrase>   phrase_from_name(std::string_view);
// The words a phrase prints, for a backend that does not write the draft's
// macro: "implementation-defined", "NTBS", "NTMBS".
std::string_view phrase_words(Phrase);

// The inlines a changed run can hold: everything but another run, because
// marks do not nest. A separate variant rather than `Inline` recursing into
// itself, so that a visitor over a run's contents dispatches a different,
// non-recursive sum type and never drives recursion (decision
// visitation-rules).
using PlainInline = std::variant<TextInline,
                                 CodeInline,
                                 RefInline,
                                 ConceptRef,
                                 PlaceholderInline,
                                 OldConceptRef,
                                 TableRef,
                                 RawInline,
                                 TermInline,
                                 PhraseInline>;

// A marked run of prose: the wording diff's `[...]{.add}` / `\added{...}`.
// One mark covers the whole run, so a backend writes one span per run rather
// than one per word. A `\tcode` inside a run is marked whole or not at all;
// the diff never splits one.
struct ChangedInline {
    Change                   change = Change::Added;
    std::vector<PlainInline> inlines;

    friend bool operator==(const ChangedInline&, const ChangedInline&) = default;
};
using Inline    = std::variant<TextInline,
                               CodeInline,
                               RefInline,
                               ConceptRef,
                               PlaceholderInline,
                               OldConceptRef,
                               TableRef,
                               RawInline,
                               TermInline,
                               PhraseInline,
                               ChangedInline>;
using Paragraph = std::vector<Inline>;

// A piece of wording with the draft's typesetting taken off: the choice
// between a macro and its no-italic-correction twin, and the line-break
// hints. Two pieces that differ only there say the same thing, which is how
// the wording diff compares them.
// The code, or the inline, without what only typesets it: line-break hints,
// which form of its macro a span takes, and the index entries (`is_wording`).
CodeText    without_typesetting(CodeText code);
PlainInline without_typesetting(PlainInline piece);

// --- description elements --------------------------------------------------
// Declaration order IS canonical [structure.specifications] order.
// (Verify against the current working draft when bumping the target.)
enum class ElementKind {
    Constraints,
    Mandates,
    Expects,     // Preconditions
    HardExpects, // Hardened preconditions
    Effects,
    Sync,    // Synchronization
    Ensures, // Postconditions
    Result,
    Returns,
    Throws,
    Complexity,
    Remarks,
    Errors,      // Error conditions
    Recommended, // Recommended practice
};

} // namespace beman::specgen::ir

template <>
inline constexpr auto beman::specgen::foundation::enumerators<beman::specgen::ir::ElementKind> = std::array{
    beman::specgen::ir::ElementKind::Constraints,
    beman::specgen::ir::ElementKind::Mandates,
    beman::specgen::ir::ElementKind::Expects,
    beman::specgen::ir::ElementKind::HardExpects,
    beman::specgen::ir::ElementKind::Effects,
    beman::specgen::ir::ElementKind::Sync,
    beman::specgen::ir::ElementKind::Ensures,
    beman::specgen::ir::ElementKind::Result,
    beman::specgen::ir::ElementKind::Returns,
    beman::specgen::ir::ElementKind::Throws,
    beman::specgen::ir::ElementKind::Complexity,
    beman::specgen::ir::ElementKind::Remarks,
    beman::specgen::ir::ElementKind::Errors,
    beman::specgen::ir::ElementKind::Recommended,
};

namespace beman::specgen::ir {

constexpr int canonical_rank(ElementKind k) { return static_cast<int>(k); }

std::string_view           element_name(ElementKind); // "constraints", ...
std::optional<ElementKind> element_from_name(std::string_view);

// Every description-body block below carries the wording diff's mark on the
// whole block (docs/plans/wording-diff.md): an added paragraph inside a kept
// element, a removed table, a new note. A block's mark renders as the same
// wrapper a node's does, placed around the block alone.
struct EquivalentTo {
    CodeText code;
    Change   change = Change::Same;

    friend bool operator==(const EquivalentTo&, const EquivalentTo&) = default;
};

// A bulleted list. Conjunctions past the sentence threshold render this way,
// and the draft uses it widely wherever a description enumerates conditions.
struct Itemize {
    std::vector<Paragraph> items;
    Change                 change = Change::Same;
    // Whether the block opens a numbered paragraph of its own (`opens_paragraph`
    // below). Unset, it belongs to the paragraph before it.
    bool opens_paragraph = false;

    friend bool operator==(const Itemize&, const Itemize&) = default;
};

// A draft two-dimensional library table (\lib2dtab2). Named fields make the
// two-column shape structural, so a backend never has to index user-provided
// JSON defensively.
struct Table2DRow {
    Paragraph header;
    Paragraph cell1;
    Paragraph cell2;

    friend bool operator==(const Table2DRow&, const Table2DRow&) = default;
};

struct Table2D {
    std::string             stable_name = {};
    Paragraph               caption     = {};
    Paragraph               column1     = {};
    Paragraph               column2     = {};
    std::vector<Table2DRow> rows        = {};
    Change                  change      = Change::Same;
    // Whether the block opens a numbered paragraph of its own (`opens_paragraph`
    // below). Unset, it belongs to the paragraph before it.
    bool opens_paragraph = false;

    friend bool operator==(const Table2D&, const Table2D&) = default;
};

// A draft flat two-column library table (\libtab2): one row per entry, no
// row-heading column -- the shape [fs.enum.file.type] and its neighbours use
// for an enumeration's meanings table (issue #74). \lib2dtab2 is a
// cross-product table and cannot represent this without a wasted third
// column or a row heading pressed into service as the constant.
struct Table1DRow {
    Paragraph cell1;
    Paragraph cell2;

    friend bool operator==(const Table1DRow&, const Table1DRow&) = default;
};

struct Table1D {
    std::string             stable_name = {};
    Paragraph               caption     = {};
    Paragraph               column1     = {};
    Paragraph               column2     = {};
    std::vector<Table1DRow> rows        = {};
    Change                  change      = Change::Same;
    // Whether the block opens a numbered paragraph of its own (`opens_paragraph`
    // below). Unset, it belongs to the paragraph before it.
    bool opens_paragraph = false;

    friend bool operator==(const Table1D&, const Table1D&) = default;
};

// A paragraph either opens a numbered paragraph or continues the paragraph
// containing the preceding list/table (decision ordered-description-bodies).
// An Elided paragraph is an unnumbered ellipsis, including in an itemdescr.
struct BodyParagraph {
    Paragraph text;
    bool      continuation = false;
    Change    change       = Change::Same;

    friend bool operator==(const BodyParagraph&, const BodyParagraph&) = default;
};
struct AuthoredCode {
    CodeText code;
    Change   change = Change::Same;
    // Whether the block opens a numbered paragraph of its own (`opens_paragraph`
    // below). Unset, it belongs to the paragraph before it.
    bool opens_paragraph = false;

    friend bool operator==(const AuthoredCode&, const AuthoredCode&) = default;
};

// Informative bodies have no recursive informative position: notes and
// examples contain prose, code, lists, and tables, but cannot nest each other.
using InformativeBlock = std::variant<BodyParagraph, Itemize, Table2D, Table1D, AuthoredCode>;
using InformativeBody  = std::vector<InformativeBlock>;
struct Note {
    InformativeBody body;
    Change          change = Change::Same;
    // Whether the block opens a numbered paragraph of its own (`opens_paragraph`
    // below). Unset, it belongs to the paragraph before it.
    bool opens_paragraph = false;

    friend bool operator==(const Note&, const Note&) = default;
};
struct Example {
    InformativeBody body;
    Change          change = Change::Same;
    // Whether the block opens a numbered paragraph of its own (`opens_paragraph`
    // below). Unset, it belongs to the paragraph before it.
    bool opens_paragraph = false;

    friend bool operator==(const Example&, const Example&) = default;
};

using BodyBlock = std::variant<BodyParagraph, Itemize, Table2D, Table1D, EquivalentTo, AuthoredCode, Note, Example>;
using DescriptionBody = std::vector<BodyBlock>;

// The wording diff's mark on a block of either body, whichever kind it is.
Change block_change(const BodyBlock&);
Change block_change(const InformativeBlock&);

// Whether a block opens a numbered paragraph, apart from the first block of a
// body, which always does. Prose says so by not being a `continuation`, and an
// extracted *Equivalent to* always opens one. A list, a table, a code block, a
// note or an example belongs to the paragraph before it unless its
// `opens_paragraph` is set, which is how the draft writes a note or an example
// that stands apart from the prose before it:
//
//     \pnum
//     \begin{note}
//
// The draft importer sets it where the draft numbered the block itself, so
// inherited wording keeps the draft's paragraph numbers (issue #321), and a
// docblock sets it with two blank lines before the block (issue #340).
bool opens_paragraph(const BodyBlock&);

struct DescriptionElement {
    ElementKind     kind = ElementKind::Effects;
    DescriptionBody body;
    // Validator-only derivation evidence, independent of the rendered body.
    bool                   derived   = false;
    std::vector<Paragraph> conjuncts = {};
    // Explicit authored correspondence claims, checked but never rendered.
    std::vector<Paragraph> coverage = {};
    // The wording diff's mark on the whole element (docs/plans/wording-diff.md).
    Change change = Change::Same;

    friend bool operator==(const DescriptionElement&, const DescriptionElement&) = default;
};

// Views of all wording sites, in authored order. Code returned here is block
// code; inline code is reached through the returned paragraphs.
std::vector<const Paragraph*> body_paragraphs(const DescriptionBody&, bool include_informative = true);
std::vector<const CodeText*>  body_code(const DescriptionBody&, bool include_informative = true);
// Adjacent lists in a merged same-kind description form one itemization.
DescriptionBody coalesce_itemizations(DescriptionBody);

// --- items -----------------------------------------------------------------
enum class IndexKind { Global, Constructor, Destructor, Member, MemberX, MemberExpos, Zombie, Misc };

} // namespace beman::specgen::ir

template <>
inline constexpr auto beman::specgen::foundation::enumerators<beman::specgen::ir::IndexKind> = std::array{
    beman::specgen::ir::IndexKind::Global,
    beman::specgen::ir::IndexKind::Constructor,
    beman::specgen::ir::IndexKind::Destructor,
    beman::specgen::ir::IndexKind::Member,
    beman::specgen::ir::IndexKind::MemberX,
    beman::specgen::ir::IndexKind::MemberExpos,
    beman::specgen::ir::IndexKind::Zombie,
    beman::specgen::ir::IndexKind::Misc,
};

namespace beman::specgen::ir {

std::string_view         index_kind_name(IndexKind); // "global", "ctor", ...
std::optional<IndexKind> index_kind_from_name(std::string_view);

struct IndexEntry {
    IndexKind   kind = IndexKind::Global;
    std::string name;
    std::string parent; // class/second index component for two-argument kinds

    friend bool operator==(const IndexEntry&, const IndexEntry&) = default;
};

struct ItemDecl {
    std::vector<CodeText>   signatures; // grouped overloads share one block
    std::vector<IndexEntry> index;      // backend-optional metadata
    // The semantic names declared by `signatures`, independent of the
    // editorial index above. A validator rendering several documents of one
    // paper unions these names so a normative entity specified by one header
    // is visible to wording generated from another (issue #109). Kept beside
    // the declarations rather than recovered from their formatted text: the
    // AST says what a declaration names (decision construct-recognition).
    std::vector<std::string> entities = {};

    friend bool operator==(const ItemDecl&, const ItemDecl&) = default;
};

struct ItemDescr {
    std::vector<DescriptionElement> elements;
    DescriptionBody                 leading = {};

    friend bool operator==(const ItemDescr&, const ItemDescr&) = default;
};

// One content predicate for rendering, routing, grouping, and coverage.
bool has_description(const ItemDescr&);

// Stable-sort elements into canonical order (authored order is irrelevant).
void canonicalize(ItemDescr&);

// A claim that a predicate of the code is covered by prose the item inherits,
// for an element the item does not author (`\asdraft`, `\covers constraints
// P`): it is carried onto the inherited element's `coverage` when the
// description is resolved, and is never rendered.
struct InheritedCover {
    ElementKind kind = ElementKind::Constraints;
    Paragraph   claim;

    friend bool operator==(const InheritedCover&, const InheritedCover&) = default;
};

struct SpecItem {
    ItemDecl  decl;
    ItemDescr descr;
    Change    change = Change::Same; // the wording diff's mark on the whole item
    // `\asdraft`: the description is the baseline's, to be resolved against
    // the baseline item this one pairs with (inherit.hpp). Whatever `descr`
    // holds is authored wording that replaces the inherited element of its
    // kind, or a derivation whose conjuncts the inherited element is
    // validated against. Cleared when resolved.
    bool                        as_draft = false;
    std::vector<InheritedCover> covers   = {};
    // This is the description beside the synopsis before it, of an
    // exposition-only concept or variable at namespace scope (design §4.3),
    // and has no declaration of its own: `\asdraft` pairs it by the
    // synopsis's declaration, and takes the description that follows the
    // baseline's declaration (inherit.hpp, issue #332). Set only with
    // `as_draft`, and cleared with it when resolved.
    bool describes_synopsis = false;
    // Where the draft importer read the item, its index entries to the end of
    // its description, and where it read the description's paragraphs, inside
    // the `itemdescr` (`NodeOrigin`).
    NodeOrigin origin       = {};
    NodeOrigin descr_origin = {};

    friend bool operator==(const SpecItem&, const SpecItem&) = default;
};

// --- document structure ----------------------------------------------------

// What the front end did with one class-body declaration (design §9).
// A synopsis is formatted code text, so nothing downstream of the front end
// can enumerate the declarations inside it; the roster below carries that
// enumeration alongside the text so the coverage invariant is checkable
// without an AST. This is an observation, not a judgment: §9's rule ("exactly
// one of merged twin, defaulted/deleted, `\omit`ted, or paired with a markup
// block") is applied by validate/validate.cpp, in Tier A, where it is testable
// with no Clang in the build.
//
// Described sorts first so a default-constructed entry -- and a hand-written
// golden that omits the key -- reports nothing.
//
// Described and Routed are both "paired with a markup block"; they differ in
// how that block was *placed*, and the distinction is load-bearing.
// An out-of-line definition's wording lands in the `\rSec` frame it is
// lexically written in and always resolves, so it is Described and its
// `section` is empty. An in-class member's wording is placed by *name* -- the
// `\ref` group above it, or an explicit `\at` -- and may resolve to nothing,
// so it is Routed and its `section` records the target it asked for. Without
// the distinction an entry routed to nowhere (an in-class member under no
// `\ref` group at all) is indistinguishable from an out-of-line one, and
// document_build::build_tree drops the former silently.
enum class Disposition {
    Described,    // wording placed lexically (an out-of-line definition)
    Routed,       // wording placed by name; see `section`
    Merged,       // `\merge`d twin
    Omitted,      // `\omit`ted
    Defaulted,    // `= default`/`= delete`, carrying no description
    Declared,     // a public data member: its declaration is its specification
    Expos,        // exposition-only member (`\expos`)
    Private,      // unmarked private: exposition, not interface (design §6)
    Undocumented, // none of the above -- what the coverage validator reports
};

} // namespace beman::specgen::ir

template <>
inline constexpr auto beman::specgen::foundation::enumerators<beman::specgen::ir::Disposition> = std::array{
    beman::specgen::ir::Disposition::Described,
    beman::specgen::ir::Disposition::Routed,
    beman::specgen::ir::Disposition::Merged,
    beman::specgen::ir::Disposition::Omitted,
    beman::specgen::ir::Disposition::Defaulted,
    beman::specgen::ir::Disposition::Declared,
    beman::specgen::ir::Disposition::Expos,
    beman::specgen::ir::Disposition::Private,
    beman::specgen::ir::Disposition::Undocumented,
};

namespace beman::specgen::ir {

std::string_view           disposition_name(Disposition); // "described", ...
std::optional<Disposition> disposition_from_name(std::string_view);

// What *sort* of class-body declaration a roster entry is, which is a
// different axis from `Disposition` (what became of it) and so a separate
// field rather than more enumerators on that one: an entry can be private
// data or a private function, exposition-only data or an exposition-only
// function, and folding the two axes together would multiply the
// dispositions rather than describe them.
//
// Only design §6's private-data nudge distinguishes Function from Data.
// Alias exists because a marked in-class alias carries routed wording; an
// unmarked alias produces no roster entry.
// `Function` sorts first for the same reason `Described` does -- a
// default-constructed entry, and a hand-written golden that omits the key,
// must report nothing.
enum class MemberKind { Function, Data, Alias };

} // namespace beman::specgen::ir

template <>
inline constexpr auto beman::specgen::foundation::enumerators<beman::specgen::ir::MemberKind> = std::array{
    beman::specgen::ir::MemberKind::Function,
    beman::specgen::ir::MemberKind::Data,
    beman::specgen::ir::MemberKind::Alias,
};

namespace beman::specgen::ir {

std::string_view          member_kind_name(MemberKind); // "function", "data"
std::optional<MemberKind> member_kind_from_name(std::string_view);

// One class-body declaration, as the front end saw it. `name` is the declared
// name (`value_or`, `operator==`, `~optional`); overloads share one, which a
// diagnostic naming the same identifier twice is accepted as the cost of not
// carrying signatures here. `section` is meaningful only for a Routed entry:
// it is the stable name that entry's markup asked to be placed under, and the
// only record that the request was made at all -- an unresolvable one is
// dropped by document_build::build_tree, leaving no other trace.
//
// `parent` is the class that declared it, which the roster's own node
// ordinarily names too (`Synopsis::name`) -- until a gathered header synopsis
// folds several classes into one node that names none of them. Then it is the
// only thing that says which class a finding is about, and the only way to ask
// a question about one class of a roster holding several (issue #45).
struct SynopsisEntry {
    std::string name;
    Disposition disposition = Disposition::Described;
    std::string section     = {}; // Routed entries only; "" means routed to nowhere
    MemberKind  kind        = MemberKind::Function;
    // The declaring class. Empty means "this synopsis's own", which is what a
    // hand-written roster looks like and what every reader treats it as; only
    // the front end's fold has anything else to say.
    std::string parent = {};

    friend bool operator==(const SynopsisEntry&, const SynopsisEntry&) = default;
};

struct Synopsis {
    // The class this synopsis is of (`optional`, `nullopt_t`), so a finding
    // about one of its members can say which class it is in: a synopsis
    // ordinarily sits at the top level, outside every `\rSec`, so there is no
    // section path to locate it by. Empty for a hand-written synopsis that
    // does not name one.
    std::string                name = {};
    CodeText                   code;
    std::vector<SynopsisEntry> roster;                // empty for a forward declaration
    Change                     change = Change::Same; // the wording diff's mark on the whole synopsis
    // The synopsis the class's own declarations generate, kept when `code` is
    // authored (`\verbatim-synopsis`) or inherited (`\asdraft-synopsis`), which
    // nothing otherwise ties to them. Validator-only: no backend reads it. The
    // check compares the two for what a paper changes without touching a word
    // (synopsis_check.hpp).
    std::optional<CodeText> declared = {};
    // Beside `declared`, the member functions the class has from its base
    // classes: one entry per class they may come from, a dependent base
    // giving one for each specialization it could be (base_members.hpp).
    // Validator-only, like `declared`, and kept with it: a synopsis line the
    // class's own declarations do not make is checked against these.
    std::vector<CodeText> declared_bases = {};
    // `\asdraft-synopsis`: `code` is the baseline synopsis of this class, with
    // the specifiers of `declared` written into it, once resolved (inherit.hpp).
    bool as_draft = false;
    // Where the draft importer read the synopsis, its code block whole
    // (`NodeOrigin`).
    NodeOrigin origin = {};

    friend bool operator==(const Synopsis&, const Synopsis&) = default;
};
struct FreeParagraph {
    Paragraph text;
    Change    change = Change::Same; // the wording diff's mark on the whole paragraph

    friend bool operator==(const FreeParagraph&, const FreeParagraph&) = default;
}; // e.g. class-general Mandates prose

struct Section;
using Node = std::variant<Section, Synopsis, SpecItem, FreeParagraph>;

struct Section {
    std::string       stable_name; // e.g. "optional.ctor"
    std::string       title;       // e.g. "Constructors"
    std::vector<Node> children;
    Change            change = Change::Same; // the wording diff's mark on the whole subtree

    // Declared here and defined below the class rather than defaulted in it:
    // a defaulted comparison makes clang resolve `children == children`
    // while `Section` is still incomplete, which instantiates `Node`'s
    // variant with its default-constructor constraint evaluated against the
    // incomplete type -- and caches that answer, so `Node node;` later fails
    // under clang (the style gates' parser) though not under GCC.
    friend bool operator==(const Section&, const Section&);
};

inline bool operator==(const Section& a, const Section& b) {
    return a.stable_name == b.stable_name && a.title == b.title && a.children == b.children && a.change == b.change;
}

// The wording diff's mark on a node of any kind, and the node with its mark
// replaced. A backend's document render reads a top-level node's mark, renders
// the node unmarked, and closes its own wrapper around the result -- so that
// adjacent marked nodes can share one wrapper, and so that mpark's wording div
// sits inside the editing-instruction div as paper mode has always put it.
Change change_of(const Node&);
Node   with_change(Node, Change);

// Every node marked `change`: the wording diff of an empty baseline against
// `nodes` is this with `Change::Added`, which is what paper mode renders.
std::vector<Node> all_marked(std::vector<Node> nodes, Change change);

// A namespace qualifier that survived the reference-resolved mapping
// (design §3.5): one resolving to a namespace that is neither `std` nor the
// header's own, so the rendered text keeps it verbatim — `detail::storage`.
// Design §9's leakage checker needs the front end to say so, because nothing
// downstream can tell a qualifier from any other identifier run: `detail`,
// `move` and a body-local `tmp` are the same three bare words to a validator,
// and only the front end resolved one of them to a namespace the reader
// cannot see.
struct ForeignNamespace {
    std::string name;      // as the qualifier writes it: `detail`
    std::string qualified; // what it resolved to: `demo::detail`

    friend bool operator==(const ForeignNamespace&, const ForeignNamespace&) = default;
};

// A bare (unqualified) name whose reference-resolved declaration this run
// never documents (issue #84): declared neither in the main file nor in a
// system header, so nothing in the rendered output declares it and no
// `\expos` sentinel stands in for it either. `ForeignNamespace`'s qualifier
// is the same idea for a *written* namespace prefix; this is the version for
// a name with no qualifier at all -- a helper sharing the header's own
// namespace, declared in an included implementation header, used bare
// because it needs no prefix to be found. A reader of the wording meets it
// with no declaration, no exposition-only entry, and no cross-reference.
//
// A spelling this run declares anywhere is never recorded here, however the
// reference resolved (decision shared-spelling-foreign-name): the check
// resolves a reference but reports it by text, and a word naming both a
// documented entity and a foreign one is evidence about neither. The front
// end drops those, so the validator never sees them -- there is no exhaustive
// declared-name channel beside this one (`paper_entities` is an explicit,
// narrow promise), and every consumer of this vector can read it as "no entity
// of this run answers to this name".
struct ForeignDeclaration {
    std::string name;   // as used: `probe_witness`
    std::string header; // the header that actually declares it

    friend bool operator==(const ForeignDeclaration&, const ForeignDeclaration&) = default;
};

// A class member named by the body of a documented function whose body the
// tool never renders (design §9): one with no `\effects-equiv` /
// `\returns-expr` marker, so nothing it does reaches the wording.
//
// Design §9's leakage checker gives such a use its *note* severity — an
// undocumented helper "appears only in non-extracted bodies" — and nothing
// downstream can see it, for a stronger reason than the one that made
// `ForeignNamespace` necessary: a non-extracted body is by construction the
// code that never becomes wording, so it has no IR representation at all.
// This is the front end saying what it saw there.
//
// `function` is the body's own owner, spelled `<class>::<name>` for a member
// (`optional::optional`) and bare for a free function, so a finding can say
// which body it is about; overloads share one spelling, and a use recorded
// from two of them collapses to one entry.
struct BodyUse {
    std::string function;
    std::string member;

    friend bool operator==(const BodyUse&, const BodyUse&) = default;
};

struct Document {
    std::vector<Node> nodes;
    // Semantic names which this generated document deliberately does not
    // render, but which the paper makes available: declarations carrying
    // `\elsewhere`, and namespace using-declarations carrying `\expos`.
    // The paper-wide validation union includes them alongside names carried
    // by rendered synopses and item declarations (issue #113).
    std::optional<std::vector<std::string>> paper_entities = {};
    // A validator-only side channel, the same shape `Synopsis::roster`
    // and `DescriptionElement::conjuncts` are — every renderer
    // ignores it. Document-level rather than per-fragment because a surviving
    // qualifier is a fact about the header, not about the one place it was
    // written: the check that reads it reports each *occurrence* in rendered
    // output, and locating those is a text match the validator already does.
    std::vector<ForeignNamespace> foreign_namespaces = {};
    // The same validator-only side channel one more time, for the bare-name
    // case `foreign_namespaces` cannot express (see `ForeignDeclaration`).
    std::vector<ForeignDeclaration> foreign_declarations = {};
    // The same validator-only side channel one more time, and
    // document-level for the same reason `foreign_namespaces` is — no node
    // owns a body the tool did not render, so there is nowhere else to hang
    // it. Sorted and deduplicated by (function, member) where the front end
    // builds it, so the emitted IR does not depend on decl visitation order.
    std::vector<BodyUse> unextracted_uses = {};

    friend bool operator==(const Document&, const Document&) = default;
};

} // namespace beman::specgen::ir

#endif // BEMAN_SPECGEN_IR_HPP
