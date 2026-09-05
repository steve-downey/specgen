# beman.specgen — architecture

`beman.specgen` is a Clang-based tool that, given a structured C++26 header, produces
standard-library **description wording** per [description]/[structure.specifications] in the
working draft, targeting three backends: draft LaTeX, mpark/wg21 pandoc markdown, and org-mode
(for the external `wg21org` exporter). It is a tool that generates wording, not a library
consumed by arbitrary toolchains ([tool-not-library](decisions/tool-not-library.md)).
Reference inputs are the Beman project's `optional.hpp` and `unexpected.hpp` headers.

This document describes what specgen **is**: the input contract, the pipeline, the wording
ontology, and the invariants each subsystem maintains. Usage is covered by
[user-guide.md](user-guide.md); the rationale behind individual structural choices lives in the
decision records under [docs/decisions/](decisions/), linked inline below.

**Section numbering is frozen.** Source comments throughout the codebase cite the sections of
this document as `design §N` / `design §N.N` (for example `design §5.1` for Constraints
derivation, `design §9` for the coverage invariant). §1–§10 and their subsection numbers are
therefore stable identifiers: content may grow, but a given number always covers the same
subject. Sections after §10 are appendix material and are not cited from source.

---

## 1. Input contract (the header conventions)

The tool assumes headers written in the Beman style:

- **Out-of-line definitions**, mirroring the structure and order of the clauses.
  The class body is declaration-only and *is* the synopsis; definitions follow,
  grouped under `// \rSec3[stable.name]{Title}` section comments.
- **Section/group comments in draft form** already present:
  `// \ref{optional.ctor}, constructors` inside the class body,
  `// \rSec3[...]{...}` headers in the definition region, trailing
  `// see~\ref{...}` comments preserved verbatim.
- **In-class definitions are permitted** in two cases: hidden friends (by nature),
  and members forced in-class by compiler bugs (dependent return types); the latter
  carry annotation.
- **Leading static_asserts**: zero or more body-local aliases may precede the
  first static_asserts in a body. Several separate asserts are semantically
  ANDed; separation exists only for diagnostic quality.
- Full C++26 (`-std=c++2c`). C++26 is also the tool's own language baseline
  ([cxx26-baseline](decisions/cxx26-baseline.md)).

Comment lines in a header are one of **three** things:

- `//!` and `/*! … */` are **specgen markup**; the docblock grammar (§4) reads them.
- `///` and `/** … */` are **Doxygen's**; specgen reads neither, and a synopsis drops them,
  because the draft does not print implementation documentation. `///` is *not* a second
  spelling of `//!`: a member documented only in Doxygen is an undocumented member as far as
  the coverage invariant (§9) is concerned.
- A plain `//` or `/* … */` is **draft-form** and survives verbatim (`\ref` group headers,
  `see~\ref` trailers, ordinary comments inside extracted bodies).

A header documented entirely in Doxygen still has to *work* (parse, exit 0, and emit IR that
reads back and renders) even though the wording it produces is empty and the validator reports
one coverage Error per declaration. The bar is robustness, and the failure mode being excluded
is silence, which would mean `@brief` prose had been promoted into wording.
Note that Doxygen accepts four comment forms and specgen has claimed two of them, so a header
that writes its Doxygen as `//!` or `/*!` is ambiguous by construction; no classification can
help it.

## 2. Architecture overview

```text
header ──▶ Clang front end ──▶ semantic IR ──▶ serializers ──▶ fragments
           (structure from AST,   (wording        (draft LaTeX,
            text from tokens,      ontology)       mpark/wg21,
            markup from comments)                  org)
```

Core principle: **AST for structure, token stream for rendering.** The AST is never
pretty-printed into output; it attaches markup, links declarations to definitions,
resolves references, and validates. All rendered code text originates from the
author's tokens, transformed subtractively.

The serialized IR is the boundary between the two halves of the codebase
([ir-boundary](decisions/ir-boundary.md)). The front end (§3) requires Clang and produces IR;
everything downstream of the IR (the grammar's lowering, the backends, the fragment split, the
validators) is Clang-free, so `render --from-ir` consumes a saved IR document without invoking
Clang at all ([clang-free-render-mode](decisions/clang-free-render-mode.md)). The front end
itself is staged: collection produces a stream of typed document events (structure markers,
declarations, docblocks, diagnostics) which later stages consume to build the document
([document-build-stages](decisions/document-build-stages.md)).

The driver (`specgen`) exposes the pipeline directly: `generate <header>` runs the front end
and the backends in one pass, with the IR never leaving the process; `generate --emit-ir`
stops at the IR and serializes it; `render --from-ir - --backend {latex,mpark,org}` picks that
serialized document back up. `--backend`, `--validate` (§9), `--paper` and
`--split <dir> [--root <name>]` (§8) mean the same thing on both rendering commands, which
share one back half, so single-pass wording equals two-pass wording byte for byte;
`dump-decls` is a front-end debugging aid. The end-to-end smoke test is
`specgen generate --emit-ir <header> | specgen render --from-ir -`.

## 3. Front end (Clang)

### 3.1 Tool setup

- `ClangTool` over a compilation database or fixed args:
  `-std=c++2c -fparse-all-comments -fsyntax-only`. `--no-compile-commands` forces the
  fixed-args path.
- Process only decls whose location is in the main file, and only *authored* ones: an
  implicit decl is compiler synthesis reporting some other entity's location — an implicit
  deduction guide sits on the constructor's or class's own tokens, and collecting one plants a
  phantom top-level decl in the middle of a class body (issue #22).
- Comments retrieved via the raw comment list; markup attachment via
  `getRawCommentForDeclNoCache`, looked up through the *described template* when the decl
  has one. Clang anchors comment search for a template decl at its `template` keyword but
  for the templated decl at its name, and rejects any comment separated from the anchor by
  one of `;{}#@` — which a requires-expression in a template-head constraint places between
  the header and the name, so a lookup through the templated decl would silently lose the
  docblock (issue #20).
- The preprocessor's **skipped ranges are recorded** during the parse, so extraction (§4.2)
  can resolve conditional compilation down to the branch Clang selected.

The Clang dependency is mandatory and unconditional (`find_package(Clang REQUIRED)`), and it
is **version-pinned** ([llvm-toolchain-pin](decisions/llvm-toolchain-pin.md)). The cache
variable `BEMAN_SPECGEN_LLVM_VERSION` (default `22.1`) is the version request: CMake's config
search globs `lib/cmake/clang*`, and an unversioned `find_package(Clang)` on a box with several
LLVMs installed side by side silently takes the newest, which is how an incompatible new LLVM
would break the build the day it lands (LLVM 23 renamed
`ASTContext::getRawCommentForDeclNoCache`, which `frontend.cpp` calls six times). A `Clang_DIR`
whose version does not match the request is *rejected* rather than used, so moving to a new
LLVM is one deliberate flag (`-DBEMAN_SPECGEN_LLVM_VERSION=23.0`) and never an accident of what
is installed. There is no Clang-free configuration.

### 3.2 Document tree

- Collect top-level decls and raw comments; sort by source offset; interleave.
- Comments matching `\rSec` / `\ref` group patterns become **structure nodes**
  (sections, synopsis group headers) instead of documentation on a decl.
- Synopsis order is source order; itemdescr order is out-of-line definition order.
  Both mirror clause order by the input contract.
- Comment classification follows the three-way vocabulary of §1: only `//!`/`/*!` chunks reach
  the docblock grammar; Doxygen chunks are collected so synopsis extraction can drop them;
  draft-form chunks are collected so it can keep them.
- **Every physical `\rSec` line is a structure event**, even when Clang coalesces adjacent
  line comments into one raw comment. Collection splits comment chunks only around recognized
  section-marker lines, retaining their absolute offsets; other multi-line chunks, including
  docblocks, remain intact. A marker whose `{title}` a formatter wrapped continues on the
  immediately following plain `//` lines — never a `///`/`//!` line or another `\rSec` — and
  the wrapped lines join back into one title with single spaces; an unclosed title is still a
  malformed-marker Warning.
- An **unrecognized draft-style section heading reports a Warning**. A draft-form line such as
  `// 22.5.3.3 Destructor[optional.dtor]` cannot open a section (it carries no `\rSec` depth),
  but it is reported instead of silently filing every following declaration into the
  previous section. The recognizer is narrower than "any bracketed dotted name":
  it requires a numbered draft-form heading ending in the stable name, so prose citations and
  Doxygen `END [optional.syn]` fence lines stay silent.

### 3.3 Redeclaration-chain attachment

- In-class declaration ↔ out-of-line definition linked via `redecls()` /
  `getDefinition()`.
- **Itemdecl text renders from the in-class declaration** (the out-of-line form
  carries `inline`, `optional<T>::` qualification, and stacked template heads the
  draft never shows).
- **Markup attaches to the out-of-line definition**, which sits in clause order.
- In-class-defined members (hidden friends, compiler-bug workarounds): markup
  attaches to the in-class definition. Their itemdescr position is inferred from
  class-body order within their `\ref` section, relative to out-of-line siblings;
  an explicit anchor (`\at`) overrides.
- **Documented namespace-scope function definitions are ordinary items.** They must carry
  specgen markup and have both semantic and lexical file contexts; the second condition keeps
  hidden friends on the in-class path, and the markup condition keeps unannotated helpers out.
  The first declaration supplies the signature; the definition supplies wording and placement.
- **Marked in-class function templates follow the ordinary member path.** A template with an
  authored description attaches to its routed section, and `\merge`/`\omit` removes a template
  declaration from the synopsis just as it does a non-template member. This also holds when
  Clang coalesces a preceding Doxygen block with the `//!` marker: directive parsing begins at
  the markup line, skipping the Doxygen prefix.

### 3.4 Synopsis extraction (subtractive)

From each decl's `CharSourceRange` via `Lexer::getSourceText`, then:

- Splice out bodies (body range → `;`), keeping `= default` / `= delete`. **The splice starts
  at the `:` of a written ctor-initializer list** when the definition is a constructor's: a
  mem-initializer list is implementation, never interface, and leaving it in place also leaked
  the private member's spelling and the dropped-qualifier rewrite into the synopsis and the
  itemdecl (issue #21). If the `:` is not found directly before the first written initializer
  across whitespace only, the splice falls back to the body brace. **A
  Clang-synthesized body is not mistaken for source**: an explicitly defaulted special member
  can gain a synthetic `CompoundStmt` after ODR-use, with a source range covering only the
  final `t` in `default`, so extraction tests `isDefaulted()`/`isDeleted()` before splicing a
  reported body (otherwise `= default;` becomes `= defaul;;`). **No edit is emitted inside a
  removed or spliced range**: the qualifier/expos/comment edit sources all filter against
  those spans, because the descending-offset apply loop would otherwise apply the inner edit
  first and its overlap watermark would suppress the removal — which is how a body naming a
  droppable qualifier once survived into the synopsis while its unqualified twin spliced
  cleanly.
- Strip docblocks/markup comments and Doxygen comments; keep draft-form comments (`\ref`,
  `see~\ref`).
- **An empty class-scope `\ref` group is not printed.** A group header is retained only when
  at least one direct declaration in its run (before the next group header) survives the
  ordinary synopsis filters. If every declaration is merged, omitted, or otherwise removed,
  exactly that physical header line is deleted, even when Clang merged it with a following
  `//! \merge`. A nested class owns its own group headers. The liveness test is AST-based, so
  a group containing only declarations in an inactive preprocessor branch is out of scope for
  this rule.
- **Direct class-scope `static_assert`s are general wording.** They are removed from the
  class synopsis regardless of access; their conditions become an adjacent paragraph per §5.2.
  Assertions in a nested class belong to that nested class.
- **Anonymous structs/unions are transparent for exposition-only fields.** The author marks
  the real nested field `\expos`; specgen ignores Clang's implicit injected projections, keeps
  the anonymous wrapper, rewrites the field and its uses, removes unmarked alternatives, and
  records only exposed nested fields in the coverage roster. Named nested classes are not
  flattened.
- **A header synopsis is an explicitly bounded ordinary synopsis.** A `\rSec` section whose
  stable name ends in `.syn` gathers the declarations and draft `\ref` headers up to an exact
  matching Doxygen fence line `/// END [same.stable]` into one anonymous synopsis child.
  Doxygen, markup, and namespace scaffolding are dropped; declaration, Ref, exposition, and
  index spans retain their semantics; a terminal `\verbatim-synopsis` payload is appended
  byte-for-byte. The gathered node has no coverage roster. `\omit` and `\merge` also suppress
  declarations inside the gathered interval; the filter reads the declaration's directive
  instead of treating every ignored collection event as suppressed, because ordinary unmarked
  helpers use that same event alternative and must still be gathered. A missing or mismatched
  fence warns and does not consume later sections.
- The `\freestanding` / `\freestanding-deleted` markers (§4.3) emit their literal comment
  suffix on the **in-class declaration** even when the marker lives on the out-of-line
  definition. Neither status enters the IR as metadata; after extraction it is ordinary
  synopsis code.
- Apply token rewrites (§3.5), then normalization (§3.6).

### 3.5 Token rewriting (reference-resolved)

All rewrites act on identifier tokens **whose AST referent is known**: never text
match (exception: dependent member uses of the class's own expos members may fall
back to name match within the class's fragments only):

- Namespace mapping: `beman::optional` → `std`; drop `std::` qualifiers inside
  the synopsis/itemdecls (names are unqualified within `namespace std`). The drop set is
  built from **prefix-minimal** paths, so a nested namespace written out in full
  (`demo::detail::storage`) renders the same as the abbreviated spelling instead of losing
  its qualifier.
- **Qualifier removal follows declaration ownership through a using-declaration.** A
  qualifier written as an implementation namespace is dropped for a use only when the
  referenced declaration is owned by a namespace already in the drop set, such as `std`. The
  implementation namespace itself is never added to the drop set; declarations it genuinely
  owns stay qualified (and then trip the leakage checker, §9, which is the point).
- Expos renaming: referents in the expos set → an ExposId sentinel, rendered as
  `\exposidnc{kebab-name}` in code contexts and `\exposid{kebab-name}` in prose.
  Namespace-scope concepts, variable templates, ordinary variables, aliases, and alias
  templates can enter that set; their resolved uses — expression and type uses alike — are
  rewritten inside extracted bodies as well as synopses, itemdecls, and derived conjuncts. `\expos` also applies to **member
  function templates**: the helper survives private-member filtering, its declared name is
  rewritten as an exposid in the class synopsis and its routed itemdecl, and extracted bodies
  rewrite calls to it through the same exposition-use path.
- Extracted-body (`*-equiv`) exposition rewriting is **reference-resolved first**: a resolved
  use of a namespace-scope expos entity loses only its own qualifier and becomes an exposid
  span; a same-named local is untouched; uses inside inactive conditional branches, consumed
  assertions, comments, or other deleted ranges produce no replacement. The class-local name
  fallback remains only for dependent member uses that Clang cannot resolve.
- `\seebelow`, `\placeholder` substitutions per markup.

### 3.6 Normalization pipeline

1. Token-rewrite with **valid-C++ sentinel identifiers** (`__SEE_BELOW__`,
   `__EXPOSID_val__`), length-padded if needed so line-breaking stays honest.
2. `clang::format::reformat()` with a `FormatStyle` derived from `getLLVMStyle()`
   (`draft_format_style()` in the front end), tuned to draft conventions:
   `AlwaysBreakTemplateDeclarations: Yes`, `RequiresClausePosition: OwnLine`,
   `IndentRequiresClause`, `BreakBeforeConceptDeclarations`, column limit ≈ 85–90, penalties
   keeping return type and name together. Tuned via golden-file diff, not a priori.
3. Parse sentinels into a **span table** (byte range → semantic kind); store
   `{text, spans}` in the IR. Backends substitute escapes per target.

## 4. Markup grammar (docblock comments)

Sparse, unordered, canonicalized by the generator. `//!` line comments (or the `/*! … */`
block form) attached to the definition; these are the only comment spellings the grammar
reads (§1). The parser is combinator-built over the raw text of one docblock
([parser-combinators](decisions/parser-combinators.md)) and performs no reference resolution:
inline spans carry raw names for the front end to resolve later.

The grammar produces its own diagnostics at three severities (the element-ordering Note, the
duplicate-element Warning, the **unknown-tag Error**), and `generate` prints them to stderr as
`<header>:<line>: <severity>: <message>`. Two consequences: markup written out of
[structure.specifications] order is reported even though the output is canonicalized either
way, and a misspelled tag reports as an Error while the IR is still emitted; `generate` does
not fail on it, so read stderr instead of trusting the exit code.

### 4.1 Description elements

Tags mirror the std macros: `\constraints`, `\mandates`, `\expects`, `\hardexpects`,
`\effects`, `\sync`, `\ensures`, `\result`, `\returns`, `\throws`, `\complexity`, `\errors`,
`\remarks`.

- `\hardexpects` is **not** `\expects`: it is a distinct description element, serialized as
  `hardexpects`, emitted as the draft's `\hardexpects` macro, and labelled
  "Hardened preconditions" by the mpark and org backends.
- Only present elements are written; only present elements are emitted.
- Canonical [structure.specifications] order is enforced on output regardless of
  authored order (the ordering lint of §9 notes deviations).
- `\pnum` and numbering are backend concerns; authors do not write them.
- Multi-paragraph: blank `//!` line separates paragraphs; next `\tag` ends the
  element.
- Backticks → `\tcode`, and backticked spans are reference-resolved: expos names
  render as `\exposid`, the leakage checker runs inside prose.
- `\item` starts an authored item on the current element; nonblank following
  lines continue it, and its punctuation is preserved. A blank ends the item.
  Once a list starts, another `\item` or element tag must follow, because the IR
  places an element's prose before its one itemization.
- `\iref{stable.name}` outside backticks is a prose cross-reference. Its target
  can be a standard subclause outside the generated document, which is why the
  dangling-local-section check does not verify it.
- `\lib2dtab2[stable.name]{caption}` starts a two-dimensional table on the
  current element. Exactly two `\column` entries precede one or more `\row`
  entries, and each row has exactly two `\cell` entries before
  `\endlib2dtab2`. Non-tag lines continue the active caption, header, or cell.
  The table is terminal within its element. One table is allowed per element
  kind in a docblock; duplicate rows belong in that table rather than in a
  duplicate same-kind element. Semantic IR retains one optional table per
  element, and backend same-kind folds preserve every table if diagnostic or
  hand-authored IR nevertheless contains more than one. Table prose participates in expos
  rewriting, leakage, drift, and *Throws:* validation like every other authored paragraph.

All three backends render authored itemizations, prose references, and tables.

### 4.2 Extraction elements

- `\effects-equiv` — *Effects: Equivalent to:* with the definition body extracted
  verbatim (rewritten, formatted), **excluding** the consumed static_assert run (§5.2).
  Conditional compilation is resolved first: inactive source ranges (recorded per §3.1) and
  the remaining conditional-directive lines are omitted, leaving only the branch Clang
  selected and no orphan `#elif`, `#else`, or `#endif`, while ordinary active comments and
  statements remain source-authored.
- `\returns-equiv` — *Returns:* from a single-return body's expression.

Extracted bodies use the same three-way comment vocabulary as everything else (§1): specgen
markup and Doxygen comments are removed from `*-equiv` code; plain draft-form comments
survive. A standalone removed comment takes its newline so it leaves no blank artifact, while
a trailing removed comment keeps the statement and its newline. Comment-like bytes inside
string and raw-string literals are not comments and remain verbatim.

### 4.3 Structural markers

The markers are enumerated in a single registry shared by the grammar and the front end
([marker-registry](decisions/marker-registry.md)):

- `\merge` — defaulted-twin declarations (triviality-propagation pairs) collapse
  into one specification entity; the marked twin is dropped from synopsis and
  itemdecl. On a namespace-scope record or class-template definition, suppress
  the complete record event instead; this lets separately authored wording
  replace an implementation definition without leaking a second synopsis.
- `\omit` — exclude a decl. A namespace-scope record or class-template
  definition is suppressed before synopsis extraction, roster construction, or
  class-head Mandates derivation.
- `\describe` — force an itemdecl for `= default` / `= delete` entities.
- `\also` (or an empty markup block) — overload joins the preceding itemdescr;
  the generator accumulates itemdecls until it reaches a described one.
  `\group id` names a primary and `\also id` joins that preceding primary when
  source-order adjacency cannot express the draft grouping. **Named groups are frame-local
  and source-ordered**: resolution happens left-to-right within one open `\rSec` frame before
  placement-key sorting, so a target must precede its follower and cannot cross a section
  boundary, even when other primaries intervene. Duplicate IDs and unresolved targets are
  Errors, but the affected item is retained. Bare `\also` and an empty docblock keep their
  adjacent push-order behavior; group IDs are transient front-end metadata and never appear
  in IR or JSON.
- `\expos` / `\expos(name)` — exposition-only; name derived by stripping trailing
  underscores then `_` → `-` kebab conversion; `\expos(name)` overrides. Rendered
  in synopsis with italic name + `// exposition only`.
- `\seebelow` — substitute *see below* for a return type: a leading one
  (deduced `auto` included) is replaced whole, and an explicit trailing
  return type keeps its SFINAE-friendly shape and renders
  `auto f(...) -> see below;`. `\seebelow noexcept`
  and `\seebelow explicit` substitute it for only the operand of the named
  conditional specifier, preserving its keyword and parentheses. Pairs with a
  `\remarks` specification of the masked type or condition.
- On an authored in-class type alias, bare `\seebelow` masks the complete
  alias RHS instead. `\impdef` is the alias-only spelling for an
  implementation-defined RHS; the two markers are mutually exclusive, and targeted
  `\seebelow` forms do not apply to an alias.
- `\constraints-in-decl` — keep the requires-clause in the itemdecl and emit no
  Constraints element (ranges-style wording) instead of the default extraction.
- `\at <anchor>` — explicit itemdescr placement for in-class-defined members.
- `\verbatim-synopsis` — terminal escape hatch for synopsis content that cannot
  be code (e.g., the `std::hash` specializations block). **Terminal means terminal**: every
  decorated line after the marker is exact synopsis code: the grammar does not parse it as
  prose or tags, and the front end does not parse or format it as C++. Written as a class
  definition's own docblock, the payload *replaces* the extracted synopsis text while the
  class's members, roster, and derived wording are collected as usual; a standalone block
  becomes an ordinary anonymous synopsis, so all three backends render it through their
  normal code-block path. Nothing meant to remain markup can follow the payload in the same
  docblock.
- `\verbatim-itemdecl` — terminal escape hatch for an exact item declaration.
  Authored description elements before the marker form the item description;
  every following decorated line becomes one span-free signature, preserving
  line breaks and bytes without C++ parsing, formatting, index inference, or
  interpretation of draft markup; draft markup such as `@\seebelow@` in the payload is
  intentionally backend-raw. Multiple declarations in one payload are not split. Written as
  a declaration's own docblock, the payload *replaces* that declaration's extracted
  itemdecl — the item appears once, never as authored *and* parsed copies; a detached block
  still stands alone, pairing with `\omit`/`\merge` on a real declaration when one exists.
- `\freestanding` — emit the literal `// freestanding` synopsis comment.
- `\freestanding-deleted` — emit the distinct `// freestanding-deleted`
  synopsis comment used for facilities that a freestanding implementation
  declares as deleted. The two are distinct flags because [lib-intro] gives the comments
  distinct meanings. Either marker may live on an out-of-line definition even though the
  suffix is emitted on the earlier in-class declaration (§3.4).

## 5. Derivations from code

The principle: [structure.specifications] read backwards. *Constraints* = removed
from overload resolution = requires-clause. *Mandates* = ill-formed = static_assert.

### 5.1 Constraints

- Source: the decl's **Sema-normalized associated constraints** (captures trailing
  requires, constrained template params, abbreviated forms uniformly).
- Phrasing rewriter over top-level `&&` conjuncts:
  - bool trait/variable → "`X` is `true`"
  - negation → "`X` is `false`"
  - concept-id → "`X` is satisfied"
  - unrecognized → verbatim "`expr` is `true`"
  - **no flattening through disjunctions**: `A && (B || C)` yields two conjuncts,
    the second verbatim.
- Default: requires-clause is removed from the itemdecl and rendered as a
  *Constraints:* paragraph. `\constraints-in-decl` overrides per decl.
- `detail::` concepts in constraints trip the leakage checker; fixits: replace
  the derived paragraph with authored `\constraints` prose, or mark that
  namespace-scope concept `\expos` so resolved uses become `\exposid` spans.

### 5.2 Mandates

- Consume the **maximal static_assert run after any leading body-local aliases**
  in the out-of-line body; flatten each condition at top-level `&&`; concatenate
  in source order (diagnostic order = specification order). Any other statement
  ends the prologue, so later assertions remain ordinary body code. Leading body-local
  aliases do not block the derivation; `*-equiv` extraction retains the aliases and removes
  only the consumed run.
- Consumed asserts are excluded from `\effects-equiv` extraction.
- `static_assert` messages are stripped.
- An authored `\mandates` replaces the derived wording. The suppressed
  derivation's conjuncts remain validator-only evidence, so drift detection can
  still warn when the authored text duplicates or contradicts a specific assert.
- Every direct class-scope static_assert routes, in source order, to one sibling
  paragraph immediately after the class synopsis in the active *general*
  subclause: "A program that instantiates `C<T>` is ill-formed unless ...".
  Conditions use the same top-level `&&` flattening and phrasing as member
  Mandates; messages are stripped. The assertions themselves are omitted from
  the synopsis (§3.4). Assertions in a nested class belong to that class instead.
- An authored `\mandates` in the class definition's *own* docblock replaces
  that paragraph and inherits its conjuncts, the same replacement rule and the
  same validator-only drift evidence a member's authored Mandates gets.

### 5.3 Conjunct rendering

Single "and"-joined sentence up to a threshold (≈3, configurable); bulleted list
beyond. Shared between Constraints and Mandates (`conjuncts.cpp`).
Authored `\item` lists bypass the threshold and preserve their own punctuation.

### 5.4 noexcept

Stays in the signature (conditional or plain); no *Throws:* derivation. The validator
cross-checks signature noexcept against any authored *Throws:* claiming otherwise, at Warning
severity (§9). There is **no** reverse check: the Lakos Rule keeps a narrow-contract
function un-`noexcept` even when it visibly never throws.

## 6. Entity rules

| Entity | Synopsis | Itemdecl/descr |
| --- | --- | --- |
| Public member, out-of-line def | yes | yes (markup at def) |
| Documented namespace-scope fn, def out of line | yes, first decl's signature | yes (markup at def, which places it) |
| `= default` / `= delete` | yes | no (unless `\describe`) |
| Defaulted twin with `\merge` | dropped | merged into primary |
| Hidden friend (`FOK_Undeclared`) | yes, `friend` kept | yes, markup in-class |
| In-class def (compiler bug) | yes | yes, markup in-class, `\at` if needed |
| Marked in-class function template | yes (unless `\merge`/`\omit`) | yes, ordinary member path |
| Private fn, unmarked | omitted, silent | — |
| Private fn/data/alias, `\expos` | yes, exposid + `// exposition only` | as referenced |
| Private alias, unmarked | omitted; rostered Private, so naming it elsewhere is a leak | — |
| Private member fn template, `\expos` | yes, exposid + `// exposition only` | as referenced; calls become expos uses |
| Field in anonymous struct/union, `\expos` | wrapper + exposed field; unmarked alternatives dropped | as referenced |
| Private data, unmarked | omitted | diagnostic nudge ("state not marked \expos") |
| In-class type alias with markup | yes | routed itemdecl; `\also` groups an adjacent alias |
| In-class type alias, unmarked | yes (synopsis-only) | none; absent from roster |
| Direct class-scope `static_assert` | removed | one adjacent general paragraph (§5.2) |
| Documented record/class-template *definition* | yes | its own description, beside the synopsis, with no itemdecl |
| Namespace concept/variable/alias, `\expos` | standalone synopsis, exposid + `// exposition only` | as referenced |
| Namespace record/class template, `\merge` or `\omit` | suppressed entirely | separately authored wording may remain |
| Documented record decl, never defined | — (no synopsis node) | yes, the declaration itself; `\also` groups |
| Record forward decl (defined elsewhere), or undocumented never-defined | none, silent | — |
| Documented namespace alias/alias template | — | yes, alias masking rules apply; `\also` groups |
| Documented namespace variable (template), concept | — | yes, the declaration whole (initializer/constraint kept) |
| Documented unsupported kind, or documented fn *declaration* | none | Error diagnostic from `generate` |

Authored in-class type aliases are **routed wording items**: an alias with a specgen docblock
becomes an ordinary itemdecl in the nearest `\ref` section (or the section named by `\at`);
adjacent aliases group with `\also`. Bare `\seebelow` masks the complete RHS as *see below*,
alias-only `\impdef` masks it as *implementation-defined*, and the two never combine (§4.3).
Marked aliases have `MemberKind::Alias` roster entries; unmarked aliases remain synopsis-only
and absent from the roster.

A class or class-template **definition**'s own docblock describes the *type*. Its
description elements become a **description-only item** — an ItemDescr carrying no
ItemDecl — placed immediately after the class synopsis and after the derived
class-scope paragraph of §5.2, in the same frame and at the same placement key, so
the three stay together through the placement sort and through a `--split`. It is an
ordinary `SpecItem` node with an empty `signatures` list rather than a new node kind:
that is what puts the class's own prose through the span, table, leakage and drift
checks every other description already goes through, where a Synopsis deliberately is
*not* a wording site. Backends emit no itemdecl block for it (§8), grouping never
treats it as an `\also` primary or follower (it has no signatures to group), and §9
rejects an item carrying neither signatures nor elements the way it rejects an empty
synopsis. `\verbatim-itemdecl` and the `*-equiv` extraction markers are Errors on a
class definition: the first has `\verbatim-synopsis` as its class-level counterpart,
and the second needs a function body to extract.

A documented record or class-template declaration the header never defines (an undefined
primary — the normal way to write an algebra whose operations a model must register) is an
ordinary wording item: its itemdecl is the declaration through its semicolon, with the
template head kept on its own line, and adjacent primaries group with `\also`. A record
declaration whose entity is defined elsewhere in the header, or an undocumented never-defined
one, contributes nothing — in particular never an empty Synopsis, whose rendering was an
empty code block (§9's empty-synopsis check keeps it that way).

Documented namespace-scope aliases, alias templates, variables, variable templates, and
concepts are likewise ordinary wording items, extracted whole through their semicolon with
constraint and initializer kept, the way the draft writes them ([concept.same],
[tuple.helper]); the alias kinds carry the in-class alias masking rules (`\impdef`, bare
`\seebelow`) and group with `\also`. `\expos` on the candidate kinds still takes the
standalone-synopsis path above instead. And the backstop for everything else: a docblock on
an entity kind that produces no wording — an enum, a deduction guide, or a function
*declaration*, whose markup belongs at the definition — is an Error from `generate` rather
than a silent drop, unless `\omit`/`\merge`/`\expos` says the silence is deliberate.

## 7. Intermediate representation

- Node vocabulary = wording ontology: Section (stable name, title, depth),
  Synopsis, ItemDecl, ItemDescr (typed description elements), WordingUnit;
  inline spans: tcode, exposid, placeholder, seebelow, impl-defined,
  ref(stable-name), library-index(optional enclosing class), note,
  example.
- **Code nodes are `{text, span table}`.**
- No typography, no paragraph numbers, no per-backend escapes.
- Concretely (`include/beman/specgen/ir.hpp`): `SpanKind`/`Span`/`CodeText`;
  `Inline`/`Paragraph` for prose; thirteen `ElementKind`s in canonical
  [structure.specifications] order (the §4.1 tags), with `DescriptionElement` carrying prose,
  one optional trailing `Itemize`, one optional table, and an optional `EquivalentTo` body;
  `ItemDecl`/`ItemDescr`/`SpecItem`; `IndexEntry`; `Section`/`Synopsis`/`FreeParagraph`/
  `Document`; and `canonicalize()`. Nodes share a common traversal substrate
  ([node-base-functor](decisions/node-base-functor.md),
  [visitation-rules](decisions/visitation-rules.md)).
- A `Synopsis` carries a **coverage roster**: its class's name plus one `SynopsisEntry`
  (disposition + `MemberKind`) per declaration. The roster is the validator's input for the
  coverage invariant and the hidden-name checks (§9). The document additionally carries two
  validator channels the front end alone can populate: `foreign_namespaces` (the
  implementation namespaces whose qualifiers survive the mapping, wherever their
  declarations live — only a `std`-rooted qualifier like `std::ranges::` is exempt, being
  the standard's own vocabulary) and `unextracted_uses` (`BodyUse`
  records naming the members reached by bodies that never become wording).
- Itemdecl index entries are optional metadata with the draft's eight editorial
  kinds: global, constructor, destructor, member, memberx, memberexpos, zombie,
  and misc. The draft backend expands them to `\indexlibrary…`; others drop
  them. Synopsis indexes use a `library-index` code span instead, because the
  migrating draft form both prints and indexes the covered name: an empty
  payload renders `\libglobal`, and an enclosing-class payload renders
  `\libmember`. Non-draft backends preserve the covered name and drop the index.
- Derive only editorial categories determined by AST identity. Constructors and
  destructors use their enclosing class; ordinary methods and documented
  in-class aliases are members; namespace functions, including hidden friends,
  are globals. Synopsis spans cover record names and visible unmarked aliases
  that have no itemdecl. Do not infer memberx, memberexpos, zombie, or misc.
  Grouped declarations retain a stable exact-value union of their indexes, so overloads
  index once and differently named aliases all survive.
- **Serializable** (`--emit-ir`): one JSON schema for emit and parse
  ([json-single-schema](decisions/json-single-schema.md)); `ir::emit_json` returns a
  `std::string` and `parse_json_document`/`_item`/`_code` read it back, with the round-trip
  invariant holding over every node, span, index, element kind, and disposition. The
  serialized form decouples the wg21org exporter, enables backend-independent golden tests,
  and provides the seam for future IR-level diff markup (add/rm wording generated from two
  header revisions; out of scope, by hand at first).

## 8. Backends (serializers)

All backends emit **standalone fragments for transclusion**; the including
document owns framing. Fragment paths derive from stable names
(`optional.ctor.tex|md|org`), default split granularity per `\rSec3`.
Rendering the same document is idempotent, but split output is not pruned: the
ordered manifest is the caller's source of truth for reconciling stale files.

There are exactly three backends, and **adding wording to one means adding it to all three**:

- **Draft LaTeX** (`backend/latex.cpp`): `\begin{itemdecl}` / `\begin{itemdescr}` / `\pnum` /
  description macros; `@\exposidnc{...}@` escapes in code (`\exposid` in prose);
  `\ref`/`\iref`; `\tcode` and `\libconcept` inlines; exact `lib2dtab2` tables; all eight
  `\indexlibrary…` itemdecl forms and the `\libglobal`/`\libmember` synopsis spans.
  `render_to_string` is the backend's whole surface.
- **mpark/wg21** (`backend/mpark.cpp`): the framework's pandoc markdown. One `::: wording`
  div per top-level node, `[#]{.pnum}` / `[#.#]{.pnum}` auto-numbering,
  `## Title [stable.name]{- .sref} {-}` headings, ```` ```cpp ```` fences, native pipe tables
  with caption anchors; index entries dropped. Paper mode (`--paper`) wraps the fragment in an
  editing-instruction div (`::: add`) and numbers its paragraphs `x`, `x+1`, `x+2`.
- **org** (`backend/org.cpp`): org for the `wg21org` exporter. `** Title [stable.name]`
  headings, `/Effects/:` element labels, `~code~` inlines, and code in
  `#+begin_codeblock` / `#+begin_itemdecl` **special** blocks, which the exporter passes to
  the draft's own listings environments. Tables are named, captioned native org tables. The
  backend numbers no paragraphs and wraps the fragment in nothing.

Shared substrate (`backend/common.hpp`): `render_code_spans` walks a span table, handing each
backend's single `escape_span` both the semantic span and the covered source spelling; the
`RenderF`/`RenderedSectionF`/`render_fmap` algebra is what all three backends are written
against ([backend-direct-algebra](decisions/backend-direct-algebra.md),
[no-typeclass-objects](decisions/no-typeclass-objects.md)). Before touching any of them:

- A description element's *label* ("Preconditions" for `\expects`, "Error conditions" for
  `\errors`) comes from `backend::common::element_label`, not from `ir::element_name`, which
  spells the LaTeX macro and is simultaneously the IR's frozen JSON key. The two agree on
  eight of thirteen kinds, which is exactly enough to make a hand-written table look right
  and be wrong.
- mpark's span escape is `$x$` (its shorthand for `@*x*@`) and it is **not**
  backslash-escaped: the framework's scanner has no backslash handling, so a literal `@` or
  `$` in code text is passed through; the note on `render_code` in `mpark.cpp` says why, so
  do not "fix" it. The mpark backend also has no whole-span special case
  where LaTeX has one, because backticks supply the code font that `\tcode` supplies there;
  `mpark.test.cpp` asserts the opposite of its `latex.test.cpp` twin on purpose.
- `SpanKind::Ref` is a cross-reference **inside a code comment** and `ir::RefInline` is the
  prose one: they render `\ref{x}` / `@[x]{- .sref}@` and `\iref{x}` / `([x]{- .sref})`
  respectively, and the parentheses belong to the prose form only. A Ref span is also the one
  kind LaTeX does *not* wrap in `@…@`, because the draft's `macros.tex` sets `texcl=true` and
  a `//` comment in a `codeblock` is already in TeX mode.
- The org backend has **no escape convention of its own**, because its code blocks are not
  code blocks: `#+begin_codeblock`/`#+begin_itemdecl` are org *special* blocks, which org's
  stock `org-latex-special-block` exports to `\begin{codeblock}` / `\begin{itemdecl}` (the
  draft's own `lstnewenvironment`s), reaching a wg21org paper via `common.tex`'s
  `\input{stdtex/macros}`. So `escapechar=@` and `texcl=true` are genuinely in force inside
  them, and the bytes there are the *same bytes* the LaTeX backend emits (verified by diffing
  the two renderings of one IR). That is why `draft_span_prose` / `draft_span_codeblock` /
  `draft_code_inline` live in `backend/common.hpp` rather than in `latex.cpp`: two backends
  writing one convention have to write it once. The blocks are not org src blocks: a src
  block's text is the exporter's to route rather than to read, and `{{{macro}}}` or `@…@`
  inside one would break `c++-ts-mode`.
- `LibraryIndex` is the sole byte-level exception to that block-interior identity: org
  preserves the covered name without the LaTeX `\libglobal`/`\libmember` wrapper, because
  paper fragments do not build the draft index. Mpark likewise keeps the covered name and
  drops the index effect.
- Out in org prose `@…@` is inert (it is a listings option), so a code inline carrying a
  span (or a literal `~`, which org's `~code~` cannot escape) leaves org for an
  `@@latex:…@@` export snippet; every other inline is org-native.

**Fragment splitting belongs to no backend.** `render --split <dir>` writes one file per
top-level section, named from its stable name; `fragments::split`
(`include/beman/specgen/fragments.hpp`) does the cutting, and a `Fragment` carries a whole
`ir::Document`, so rendering one is the ordinary `render_to_string(document)` call. Do not add
a fragment-shaped entry point to a backend: per-backend framing already lands per fragment
(mpark's `::: wording` div, nothing at all in org) because a fragment *is* a
document; the file extension is the only backend fact in the scheme, and it lives in the
driver. Consequences:

- Top-level nodes outside every section (the class synopses) gather into one root fragment
  whose name is the longest dotted prefix the section names share (`optional`), or whatever
  `--root` says.
- A section with no stable name, two fragments claiming one name, or a name that is not a
  plain dotted identifier is refused rather than written somewhere surprising
  (`usable_as_file_name` is the path-traversal boundary), as are loose nodes with no
  derivable name.
- Nothing is pruned: a renamed section leaves the previous run's file on disk, and the
  ordered manifest specgen prints on stdout (document order, which the directory listing
  loses) is what a caller reconciles against.

## 9. Validation

`render --validate` runs eight checks over an IR document. An Error makes validation skip
rendering; Warnings and Notes leave the exit code at 0. Findings follow a shared severity and
reporting taxonomy ([expected-error-taxonomy](decisions/expected-error-taxonomy.md)).

1. **Coverage invariant** (Error, either direction): every class-body declaration is exactly
   one of: merged twin, defaulted/deleted, `\omit`ted, or paired with a markup block; every
   markup block resolves to a declaration; and every `\ref` group a header uses has a matching
   `\rSec`. The check reads the synopsis roster (§7). A gathered header synopsis (§3.4) has
   no roster and is exempt.
2. **Leakage checker**: every name token in rendered output must resolve to a std-visible
   documented entity, an expos-set member, a template parameter, or a local of the extracted
   body. Three clauses at two severities:
   - **Error** if the leak lands in wording text: an itemdecl, an *Equivalent to:* body, or
     backticked prose naming a member the reader cannot see (e.g. an extracted body calling a
     private `hard_reset()`).
   - **Error** for an unresolved implementation-namespace qualifier anywhere in rendered
     output: wording, itemdecl, and a **synopsis**. A finding
     requires an actual qualifier occurrence: a bare English word matching a recorded foreign
     namespace is silent; only that identifier followed by `::` reports. (Hidden roster-name
     checks still inspect every identifier occurrence.) In a synopsis the roster half runs
     for exactly one hidden disposition, **Private** — an unmarked private member's own
     declaration is unconditionally dropped there, so an occurrence of its name is a
     surviving declaration reaching for it (`using type = raw;` naming the elided private
     alias) and cannot be the self-report a `\merge`d or `\omit`ted name would be.
   - **Note** if an undocumented helper **function** appears only in bodies the tool never
     extracts: a documented function without `\effects-equiv` is never printed, so the front
     end records what such bodies name (`unextracted_uses`, §7) and the validator notes any
     the roster says the reader cannot see. Only functions report here: a private data member
     read by a body is the private-data nudge's business, reported once at its declaration
     rather than once per body. A name the wording already leaked draws the Error and no
     note; that is the "only".

   Fixit trichotomy in all cases: mark `\expos`, rewrite in documented terms, or demote to
   authored prose.
3. **noexcept ↔ *Throws:* cross-check** (Warning): an authored *Throws:* contradicting a
   `noexcept` signature. Warn-only for a load-bearing reason: an Error would skip rendering,
   and a wrong *Throws:* paragraph should not cost the whole document. No reverse check, per
   §5.4.
4. **Mandates/Constraints drift** (Warning): authored prose vs. the suppressed derivation's
   conjuncts (§5.2), located at the specific assert, firing on duplication or contradiction.
5. **Ordering lint** (Note): authored element order vs. canonical. This rule lives in the
   docblock grammar itself (§4); the pipeline's job is to carry the grammar's diagnostics out
   to the driver rather than compute anything new.
6. **Unmarked private data nudge** (Note): fires only on a class that already marks something
   `\expos` and still leaves a data member unmarked — narrower than "any unmarked private
   data", which reports the filler members of nearly every fixture. The rule it
   enforces: a class that uses `\expos` should mark *all* the state its wording leans on.
7. **Empty synopsis** (Error): a Synopsis whose code is empty, which every backend renders as
   an empty fenced code block — a blank box where a declaration should be. The front end no
   longer produces one (a record declaration that defines nothing becomes an ordinary
   itemdecl, or no node at all — §6), so a finding here means hand-written IR or a front-end
   regression.
8. **Empty item** (Error): a SpecItem carrying neither signatures nor description elements.
   An item with no signatures is legitimate — a class's own description (§6) is exactly that —
   and one with no description is the ordinary undescribed itemdecl; carrying neither renders
   as nothing at all, which is the same silent drop as the check above in the shape that
   leaves no blank box to notice it by.

## 10. Testing strategy

- **Grammar unit tests**: parser over strings, no clang.
- **Golden files per backend**; IR goldens via `--emit-ir`. The golden harness
  ([golden-test-harness](decisions/golden-test-harness.md), `tests/golden/`,
  `run-golden.cmake`) registers each case as ctest → driver → `cmake -E compare_files`, in
  five modes:
  - `MODE=render` — render checked-in `expected.json` through a backend; a `BACKEND` argument
    registers `golden.<case>.<backend>` comparing `expected.md` / `expected.org`, so one
    input renders to one expected file per backend.
  - `MODE=validate` — stderr compared, exit 1 required unless `EXPECT_EXIT` says otherwise.
  - `MODE=generate` — run the front end over a corpus header; each case also registers a
    `.roundtrip` sibling (emit → parse → emit), a `.validate` sibling running
    `render --validate` over the checked-in `expected.json`, and a `.singlepass` sibling
    (`run-single-pass.cmake`) demanding that `generate <header>` render the same bytes
    `render --from-ir <expected.json>` does — the single-pass path against the two-pass one,
    with no third golden to maintain.
  - `MODE=diagnose` — a header's build-time stderr compared, run from the header's own
    directory so printed paths are relative.
  - `MODE=split` — the driver runs **twice** from a scratch directory with a relative
    `--split wording`, and the case compares the printed manifest, the *set* of files
    written, and each file against `expected.<backend>/`. Any change to a backend's
    document-level framing therefore moves the fragments goldens as well as that backend's
    own. `make goldens` regenerates wholesale.
- **Corpus** (`tests/corpus/*.hpp`): hand-curated Beman-style headers, hermetic by design
  ([hermetic-corpus](decisions/hermetic-corpus.md)): no external includes, so generation is
  reproducible anywhere. Corpus headers are clang-format-controlled, and their formatting is
  part of the extracted output, so goldens are regenerated after a header is reformatted.
  Every corpus header must satisfy the §9 coverage invariant and, by convention, validate
  clean, with four standing findings, each **pinned rather than skipped**:
  - `spec_namespace.hpp` is the fixture for a foreign qualifier surviving at all; it is
    registered `NO_VALIDATE` and its Error is pinned by its own validate golden. That is the
    opt-out to reuse if another header ever needs one; pin the diagnostic, never just skip
    the case. `spec_foreign_include.hpp` takes the same opt-out for the qualifier whose
    namespace is declared in an included header rather than the main file, and
    `spec_private_alias.hpp` for a synopsis declaration naming an elided private alias.
  - `spec_optional.hpp`'s default constructor calls the undocumented `hard_reset()`, and a
    validate golden pins the resulting Note. It needs no `NO_VALIDATE`: a Note leaves the
    exit code at 0.
- **Paired fixtures watch differential invariants.** `spec_doxygen.hpp` is
  `spec_widget.hpp` with Doxygen added at every position, and its expected IR is the widget
  skeleton's modulo the rename; if comment handling changes, the signal is those two goldens
  diverging together. `spec_doxygen_only.hpp` (a header documented entirely in
  Doxygen) has two goldens: the parse-and-round-trip, and a validate golden expecting one
  coverage Error per declaration and nothing else. Those Errors are the *desired* output;
  the failure the fixture exists to catch is silence, which would mean `@brief` prose had
  been promoted into wording.
- **The examples document's output is checked, not asserted.** Every command in
  `docs/examples.org` is a script under `examples/cli/`, and every output it shows is a file
  that script actually wrote, captured under `examples/cli/output/` by
  `examples/cli/run-all.sh` and checked in. Two kinds of ctest case cover them
  (`ctest -R examples.`): `examples.parity.*` compares a captured file against its golden
  twin and runs no binary at all: transitive proof, since the golden case already pins that
  file to what specgen printed; `examples.rerun.*` re-runs a script with the build's driver
  and compares everything it wrote, covering outputs with no golden twin. A script claiming a
  golden twin must reproduce the harness's invocation exactly, `--no-compile-commands`
  included, or parity breaks; captured files must contain no absolute path, which is why the
  diagnostics capture runs from the header's own directory and the fragment splits run from
  inside their output directory with a relative `--split wording`. Captures are regenerated
  from a non-sanitizer build and committed.
- **Acid test (draft)**: reverse-engineer [optional] into a Beman-style header;
  diff generated LaTeX against the std source. The residual diff enumerates
  missing markers and tunes the FormatStyle. The measurement against the untouched upstream
  `beman/optional/optional.hpp` is likewise out of tree, reproduced by the acid procedure
  instead of ctest (the corpus stays hermetic).
- **Acid test (mpark)**: render fragments through the wg21 pandoc pipeline.
- **Golden trio** (from optional.hpp, covering every derivation path):
  `emplace` (Mandates from assert + effects prose), `value_or` (Mandates +
  `\effects-equiv`), a converting constructor (long Constraints list, `detail::`
  leakage path).
- **Known unit-test-only guards** — holes the golden suite cannot see, watched by unit tests
  alone: no corpus string holds a control character at or above 0x10, so nothing in the
  golden suite can distinguish `{:02x}` from `{:02X}` in `write_json_string` (the IR unit tests are the only check on
  JSON-escaping changes); and no generate-mode golden renders to
  `.tex`, so the LaTeX span-escape rules above are guarded by `latex.test.cpp` plus the one
  `.tex` fragments golden, which is where the bare `\ref{…}` in a synopsis group comment is
  covered by something other than a unit test.

---

## 11. Component inventory

The build assembles the tool from these components, each following the shared CMake recipe
([component-recipe](decisions/component-recipe.md)). Shared low-level utilities live in
`foundation/`, consumed as a subtree with a local divergence log
([subtree-consumption](decisions/subtree-consumption.md)).

- **Semantic IR** — `include/beman/specgen/ir.hpp`, `src/beman/specgen/ir.cpp`.
  The §7 node vocabulary: spans and `CodeText`, prose inlines and paragraphs, the thirteen
  `ElementKind`s in canonical order, `DescriptionElement` + `EquivalentTo`,
  `ItemDecl`/`ItemDescr`/`SpecItem`, `IndexEntry`, `Section`/`Synopsis`/`FreeParagraph`/
  `Document`, the coverage roster (`Disposition`, `MemberKind`, `SynopsisEntry`), the
  validator channels (`foreign_namespaces`, `unextracted_uses`), and `canonicalize()`.
- **IR serialization** — same files. JSON round trip: `emit_json` (returns a `std::string`)
  and `parse_json_document`/`_item`/`_code`; one schema for both directions.
- **Docblock grammar** — `include/beman/specgen/docblock.hpp`,
  `src/beman/specgen/docblock.cpp`. Decoration stripping for the §1 comment vocabulary, the
  §4.1 element tags, the §4.3 structural markers, the multi-paragraph rule, backtick inlines,
  cross-checks, three-severity diagnostics.
- **Lowering** — `include/beman/specgen/lower.hpp`, `src/beman/specgen/lower.cpp`.
  `lowering::lower`: `grammar::Docblock` → `ir::ItemDescr`; prose inline mapping, marker
  interpretation, placement/visibility directives carried out of band, canonicalization.
- **Conjunct renderer** — `include/beman/specgen/conjuncts.hpp`,
  `src/beman/specgen/conjuncts.cpp`. The §5.3 sentence/itemize rendering shared by
  Constraints and Mandates.
- **LaTeX backend** — `include/beman/specgen/backend/latex.hpp`,
  `src/beman/specgen/backend/latex.cpp`. Draft LaTeX per §8; `render_to_string` is the whole
  surface.
- **mpark backend** — `include/beman/specgen/backend/mpark.hpp`,
  `src/beman/specgen/backend/mpark.cpp`. The wg21 framework's pandoc markdown per §8,
  including paper mode.
- **org backend** — `include/beman/specgen/backend/org.hpp`,
  `src/beman/specgen/backend/org.cpp`. Org for the `wg21org` exporter per §8.
- **Backend substrate** — `include/beman/specgen/backend/common.hpp`. `render_code_spans`,
  the `RenderF`/`render_fmap` algebra, `element_label`, and the shared draft span/inline
  spellings (`draft_span_prose`, `draft_span_codeblock`, `draft_code_inline`).
- **Fragment split** — `include/beman/specgen/fragments.hpp`,
  `src/beman/specgen/fragments.cpp`. `fragments::split` per §8; backend-agnostic, with
  `usable_as_file_name` as the path boundary.
- **Validators** — `include/beman/specgen/validate/`. The §9 checks as cases of a shared
  validation algebra over the IR.
- **Front end** — `include/beman/specgen/frontend/frontend.hpp`,
  `src/beman/specgen/frontend/frontend.cpp`. §3 end to end: `collect_interleaved`,
  `build_document`, `attach_function`, `collect_inclass_items`, `extract_synopsis`, the
  `build_omit_set`/`build_expos_set`/`build_namespace_drop_set` directive sets,
  `format_and_recover` (the §3.6 sentinel pipeline), `draft_format_style()`.
- **Driver** — `tools/specgen/main.cpp`. The §2 CLI: `generate` (`--emit-ir` or the
  single-pass render), `render` (`--from-ir`), the wording options both share
  (`--backend`, `--validate`, `--split`/`--root`, `--paper`) in `emit_wording`, and
  `dump-decls`.
- **Golden harness** — `tests/golden/`, `tests/golden/run-golden.cmake`,
  `tests/golden/run-single-pass.cmake`. The §10 modes: render (per backend), validate,
  generate (with `.roundtrip`/`.validate`/`.singlepass` siblings), diagnose, split.
- **Corpus** — `tests/corpus/*.hpp`. The hermetic fixture headers of §10, one per feature
  area (markers, constraints, mandates, in-class members, expos, seebelow, namespaces, equiv
  bodies, conditional compilation, Doxygen, aliases, verbatim blocks, indexes, empty ref
  groups, header synopses, and more).
- **Unit tests** — `tests/beman/specgen/**`. Catch2 cases per component, including the §10
  unit-test-only guards.
- **Examples** — `examples/cli/`, `docs/examples.org`, `examples/emit_ir.cpp`. The checked
  example scripts and captured outputs of §10, plus a minimal
  parse-docblock → hand-built-IR → JSON demo.
- **CI** — `.github/workflows/ci_tests.yml`. The Beman preset lanes plus an LLVM-installing
  front-end job.

## 12. Cross-cutting rules

- **Tool, not library** ([tool-not-library](decisions/tool-not-library.md)): `beman.specgen`
  is an executable that generates wording. This is what justifies the narrow supported
  toolchain set and the C++26 floor
  ([cxx26-baseline](decisions/cxx26-baseline.md)).
- **Output discipline** ([format-print-output](decisions/format-print-output.md)):
  interpolate with `std::format`, print with `std::print`/`std::println` to a `FILE*`. Do not
  build a message by `+` and `std::to_string`, and do not thread a `std::ostream&` through a
  producer so its leaves can `<<` into it: return the text and write it once. Streams keep
  one job: moving bytes that are already built (which is why `read_all` in the driver slurps
  with one). Concretely: `ir::emit_json` returns a `std::string`,
  `foundation::json_writer`'s scopes append to a `std::string&`, and each backend's
  `render_to_string` is its entire surface.
- **One wording convention, three writers**: anything that changes what wording *says* lands
  in all three backends (§8), and anything that changes a backend's document-level framing
  moves the fragments goldens too (§10).
- **Pin, don't skip**: a fixture that legitimately produces a diagnostic gets that diagnostic
  pinned by a golden (§10), never a skipped check.
- **Diagnostics are said out loud**: the grammar's findings, unrecognized draft headings, and
  fence mismatches all report rather than silently self-correct (§3.2, §4), while `generate`
  still emits IR where it can — stderr, not the exit code, is the record.
