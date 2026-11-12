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
    ExposId,      // payload = kebab-case exposition-only name
    SeeBelow,     // "see below" substitution
    ImplDefined,  // "implementation-defined" substitution
    Placeholder,  // payload = placeholder text
    Ref,          // payload = stable name (cross-reference inside a comment)
    LibraryIndex, // payload = enclosing class, or empty for a global name
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

    friend bool operator==(const Span&, const Span&) = default;
};

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

struct CodeText {
    std::string            text;
    std::vector<Span>      spans;        // sorted by begin, non-overlapping
    std::vector<ChangeRun> changes = {}; // the wording diff's marks, usually none

    friend bool operator==(const CodeText&, const CodeText&) = default;
};

// --- prose -----------------------------------------------------------------
struct TextInline {
    std::string text;

    friend bool operator==(const TextInline&, const TextInline&) = default;
};
struct CodeInline {
    CodeText code;

    friend bool operator==(const CodeInline&, const CodeInline&) = default;
}; // \tcode; spans allow exposid etc.
struct RefInline {
    std::string stable_name;

    friend bool operator==(const RefInline&, const RefInline&) = default;
};
// A library concept name, rendered with the draft's \libconcept macro (not
// \tcode). Constraints prose uses the "`X` models `C`" form, where `C` is a
// ConceptRef; see the Constraints derivation (design §5.1).
struct ConceptRef {
    std::string name;

    friend bool operator==(const ConceptRef&, const ConceptRef&) = default;
};
// The inlines a changed run can hold: everything but another run, because
// marks do not nest. A separate variant rather than `Inline` recursing into
// itself, so that a visitor over a run's contents dispatches a different,
// non-recursive sum type and never drives recursion (decision
// visitation-rules).
using PlainInline = std::variant<TextInline, CodeInline, RefInline, ConceptRef>;

// A marked run of prose: the wording diff's `[...]{.add}` / `\added{...}`.
// One mark covers the whole run, so a backend writes one span per run rather
// than one per word. A `\tcode` inside a run is marked whole or not at all;
// the diff never splits one.
struct ChangedInline {
    Change                   change = Change::Added;
    std::vector<PlainInline> inlines;

    friend bool operator==(const ChangedInline&, const ChangedInline&) = default;
};
using Inline    = std::variant<TextInline, CodeInline, RefInline, ConceptRef, ChangedInline>;
using Paragraph = std::vector<Inline>;

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

    friend bool operator==(const AuthoredCode&, const AuthoredCode&) = default;
};

// Informative bodies have no recursive informative position: notes and
// examples contain prose, code, lists, and tables, but cannot nest each other.
using InformativeBlock = std::variant<BodyParagraph, Itemize, Table2D, Table1D, AuthoredCode>;
using InformativeBody  = std::vector<InformativeBlock>;
struct Note {
    InformativeBody body;
    Change          change = Change::Same;

    friend bool operator==(const Note&, const Note&) = default;
};
struct Example {
    InformativeBody body;
    Change          change = Change::Same;

    friend bool operator==(const Example&, const Example&) = default;
};

using BodyBlock = std::variant<BodyParagraph, Itemize, Table2D, Table1D, EquivalentTo, AuthoredCode, Note, Example>;
using DescriptionBody = std::vector<BodyBlock>;

// The wording diff's mark on a block of either body, whichever kind it is.
Change block_change(const BodyBlock&);
Change block_change(const InformativeBlock&);

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

struct SpecItem {
    ItemDecl  decl;
    ItemDescr descr;
    Change    change = Change::Same; // the wording diff's mark on the whole item

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
