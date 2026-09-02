// include/beman/specgen/ir.hpp                                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — semantic IR for standard-library description wording.
// Node vocabulary is the wording ontology of [structure.specifications];
// no typography, no paragraph numbers, no backend escapes.
// See docs/architecture.md §7.

#ifndef BEMAN_SPECGEN_IR_HPP
#define BEMAN_SPECGEN_IR_HPP

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

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

struct Span {
    std::size_t begin = 0; // byte offsets into CodeText::text, [begin, end)
    std::size_t end   = 0;
    SpanKind    kind  = SpanKind::ExposId;
    std::string payload;
};

struct CodeText {
    std::string       text;
    std::vector<Span> spans; // sorted by begin, non-overlapping
};

// --- prose -----------------------------------------------------------------
struct TextInline {
    std::string text;
};
struct CodeInline {
    CodeText code;
}; // \tcode; spans allow exposid etc.
struct RefInline {
    std::string stable_name;
};
// A library concept name, rendered with the draft's \libconcept macro (not
// \tcode). Constraints prose uses the "`X` models `C`" form, where `C` is a
// ConceptRef; see the Constraints derivation (design §5.1).
struct ConceptRef {
    std::string name;
};
using Inline    = std::variant<TextInline, CodeInline, RefInline, ConceptRef>;
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
    Errors, // Error conditions
};

constexpr int canonical_rank(ElementKind k) { return static_cast<int>(k); }

std::string_view           element_name(ElementKind); // "constraints", ...
std::optional<ElementKind> element_from_name(std::string_view);

struct EquivalentTo {
    CodeText code;
};

// A bulleted list. Conjunctions past the sentence threshold render this way,
// and the draft uses it widely wherever a description enumerates conditions.
struct Itemize {
    std::vector<Paragraph> items;
};

// A draft two-dimensional library table (\lib2dtab2). Named fields make the
// two-column shape structural, so a backend never has to index user-provided
// JSON defensively.
struct Table2DRow {
    Paragraph header;
    Paragraph cell1;
    Paragraph cell2;
};

struct Table2D {
    std::string             stable_name = {};
    Paragraph               caption     = {};
    Paragraph               column1     = {};
    Paragraph               column2     = {};
    std::vector<Table2DRow> rows        = {};
};

struct DescriptionElement {
    ElementKind                 kind = ElementKind::Effects;
    std::vector<Paragraph>      paragraphs;      // prose (possibly empty)
    std::optional<Itemize>      itemize;         // enumerated conditions
    std::optional<Table2D>      table      = {}; // authored two-dimensional table
    std::optional<EquivalentTo> equivalent = {}; // "Equivalent to:" code
    // Produced by derive_constraints/derive_mandates (design §5.2)
    // rather than authored in a docblock. `conjuncts` -- one paragraph per
    // derived conjunct, before conjuncts::render_into folds them into
    // `paragraphs`/`itemize` -- is a validator-only side channel, the same
    // shape as `Synopsis::roster`: every renderer ignores it, and it
    // exists so the drift validator can point at the specific conjunct
    // instead of a joined sentence with no conjunct boundaries left in it.
    bool                   derived   = false;
    std::vector<Paragraph> conjuncts = {};
};

// --- items -----------------------------------------------------------------
enum class IndexKind { Global, Constructor, Destructor, Member, MemberX, MemberExpos, Zombie, Misc };

struct IndexEntry {
    IndexKind   kind = IndexKind::Global;
    std::string name;
    std::string parent; // class/second index component for two-argument kinds
};

struct ItemDecl {
    std::vector<CodeText>   signatures; // grouped overloads share one block
    std::vector<IndexEntry> index;      // backend-optional metadata
};

struct ItemDescr {
    std::vector<DescriptionElement> elements;
};

// Stable-sort elements into canonical order (authored order is irrelevant).
void canonicalize(ItemDescr&);

struct SpecItem {
    ItemDecl  decl;
    ItemDescr descr;
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
    Expos,        // exposition-only member (`\expos`)
    Private,      // unmarked private: exposition, not interface (design §6)
    Undocumented, // none of the above -- what the coverage validator reports
};

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

std::string_view          member_kind_name(MemberKind); // "function", "data"
std::optional<MemberKind> member_kind_from_name(std::string_view);

// One class-body declaration, as the front end saw it. `name` is the declared
// name (`value_or`, `operator==`, `~optional`); overloads share one, which a
// diagnostic naming the same identifier twice is accepted as the cost of not
// carrying signatures here. `section` is meaningful only for a Routed entry:
// it is the stable name that entry's markup asked to be placed under, and the
// only record that the request was made at all -- an unresolvable one is
// dropped by document_build::build_tree, leaving no other trace.
struct SynopsisEntry {
    std::string name;
    Disposition disposition = Disposition::Described;
    std::string section     = {}; // Routed entries only; "" means routed to nowhere
    MemberKind  kind        = MemberKind::Function;
};

struct Synopsis {
    // The class this synopsis is of (`optional`, `nullopt_t`), so a finding
    // about one of its members can say which class it is in: a synopsis
    // ordinarily sits at the top level, outside every `\rSec`, so there is no
    // section path to locate it by. Empty for a hand-written synopsis that
    // does not name one.
    std::string                name = {};
    CodeText                   code;
    std::vector<SynopsisEntry> roster; // empty for a forward declaration
};
struct FreeParagraph {
    Paragraph text;
}; // e.g. class-general Mandates prose

struct Section;
using Node = std::variant<Section, Synopsis, SpecItem, FreeParagraph>;

struct Section {
    std::string       stable_name; // e.g. "optional.ctor"
    std::string       title;       // e.g. "Constructors"
    std::vector<Node> children;
};

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
};

// A class member named by the body of a documented function whose body the
// tool never renders (design §9): one with no `\effects-equiv` /
// `\returns-equiv` marker, so nothing it does reaches the wording.
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
};

struct Document {
    std::vector<Node> nodes;
    // A validator-only side channel, the same shape `Synopsis::roster`
    // and `DescriptionElement::conjuncts` are — every renderer
    // ignores it. Document-level rather than per-fragment because a surviving
    // qualifier is a fact about the header, not about the one place it was
    // written: the check that reads it reports each *occurrence* in rendered
    // output, and locating those is a text match the validator already does.
    std::vector<ForeignNamespace> foreign_namespaces = {};
    // The same validator-only side channel one more time, and
    // document-level for the same reason `foreign_namespaces` is — no node
    // owns a body the tool did not render, so there is nowhere else to hang
    // it. Sorted and deduplicated by (function, member) where the front end
    // builds it, so the emitted IR does not depend on decl visitation order.
    std::vector<BodyUse> unextracted_uses = {};
};

// --- serialization (--emit-ir) --------------------------------------------
// Emission returns the JSON rather than writing it into an ambient stream
// (decision format-print-output): the one caller that has a sink writes the
// result once, at the end. This mirrors `backend::latex::render_to_string`,
// and is the same "produce a value, write at the very end" shape
// backend/latex.cpp's prolog argues for. The *format* is the frozen contract,
// not this signature.
std::string emit_json(const Document&);
std::string emit_json(const Node&);
std::string emit_json(const SpecItem&);
std::string emit_json(const CodeText&);

// --- deserialization -------------------------------------------------------
// The IR is the contract between the clang-dependent front end and everything
// downstream, so the emitted form has to be readable back: it is what lets the
// external orgwg21 exporter and the backend goldens work without linking clang.
//
// Accepts the JSON these emitters produce. Key order is not significant and
// insignificant whitespace is permitted, so hand-edited goldens stay valid.
struct ParseError {
    std::size_t offset = 0; // byte offset into the input
    std::string message;
};

// A single fallible step (decision expected-error-taxonomy): JSON parsing is
// fail-fast, and in a serialization format the first error is definitive, so
// the taxonomy's carrier is `std::expected<T, ParseError>` plus
// `.and_then(...)`, not Validation-style accumulation (the taxonomy makes IR
// JSON parsing one of the two named fail-fast cases, alongside `parse_rsec`).
std::expected<Document, ParseError> parse_document(std::string_view);
std::expected<SpecItem, ParseError> parse_item(std::string_view);
std::expected<CodeText, ParseError> parse_code(std::string_view);

// Transitional shims, kept so existing callers migrate on their own
// schedule: thin wrappers over the `expected` entry points above,
// translating a failure into `std::nullopt` plus an optional out-param
// instead of an `unexpected`. New code should call
// parse_document()/parse_item()/parse_code() directly.
[[deprecated("use parse_document(); transitional shim")]] std::optional<Document>
parse_json_document(std::string_view, ParseError* = nullptr);
[[deprecated("use parse_item(); transitional shim")]] std::optional<SpecItem> parse_json_item(std::string_view,
                                                                                              ParseError* = nullptr);
[[deprecated("use parse_code(); transitional shim")]] std::optional<CodeText> parse_json_code(std::string_view,
                                                                                              ParseError* = nullptr);

} // namespace beman::specgen::ir

#endif // BEMAN_SPECGEN_IR_HPP
