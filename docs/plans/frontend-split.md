# Plan: split `frontend.cpp` into components

This plan settles the open half of [frontend-header-facilities](header-discipline-review.md#frontend-header-facilities).
The compile-argument handling and `parse_rsec` already moved out. What remains is `frontend.hpp`'s four entry
points: `smoke_check`, `install_crash_handler`, `collect_interleaved` and `build_document`. Behind them sits the
8153-line `frontend.cpp`, which holds the whole front end in one anonymous namespace. The header is the small part
of the problem.

## Goal

Every facility in `frontend.cpp` that another facility uses becomes a component: a header, a `.cpp`, and a test
file. The components form a DAG. A piece that needs no Clang leaves the front-end library and drops into the core
(decision recorded under [frontend-header-facilities](header-discipline-review.md#frontend-header-facilities): the
tier split is not a constraint, the DAG is). No step changes output: goldens and captures do not move, and a golden
diff in any step is a defect in that step.

A side effect worth having: a change to one derivation recompiles one TU, not 8000 lines of AST code.

## What the file holds today

`frontend.cpp` is already organized by `// --- <topic> ---` banners, and the banners are already in dependency
order. A script found every top-level definition, assigned it to the section it sits in, and recorded each use of
it from another section. Every use points backwards, to an earlier section. No section uses a later one, so the
source order is a topological order. Cutting along the banners cannot produce a cycle. The row numbers below are
for the "used by" column only.

| # | Lines | Section | Size | Used by # |
| --- | --- | --- | --- | --- |
| 0 | 1–522 | prelude: `smoke_check`, `install_crash_handler`, `parse_header`, decl collection | 522 | 1, 15, 17–19 |
| 1 | 523–695 | file helpers: `file_buffer`, `file_comments`, `parse_ref`, skipped ranges | 173 | 4, 7, 12, 14, 17–19 |
| 2 | 696–752 | normalization: `draft_format_style`, `format_code` | 57 | 5, 9 |
| 3 | 753–1119 | namespace mapping: `expos_uses`, `namespace_qualifier_edits` | 367 | 7–9, 12, 15, 17 |
| 4 | 1120–1531 | docblock lookup and synopsis edits: `has_docblock`, `SynopsisEdit`, masks | 412 | 7–9, 11–19 |
| 5 | 1532–1718 | sentinels: `SpanInfo`, `recover_sentinels`, `format_and_recover` | 187 | 6–9, 12, 17 |
| 6 | 1719–2117 | exposition-only uses: `apply_expos_sentinels`, member projections | 399 | 7–9, 11, 12, 14–19 |
| 7 | 2118–3268 | merged twins, then (from 2275) synopsis extraction: `extract_synopsis` | 1151 | 8–12, 17 |
| 8 | 3269–3525 | redeclaration-chain attachment: `extract_itemdecl` | 257 | 12 |
| 9 | 3526–3843 | Constraints derivation: `derive_constraints`, `phrase_conjunct` | 318 | 10–12 |
| 10 | 3844–3946 | Mandates derivation | 103 | 12 |
| 11 | 3947–4122 | class-head `static_assert`s: `attach_class_description` | 176 | 17 |
| 12 | 4123–4960 | `\effects-equiv` / `\returns-expr` body extraction: `attach_function` | 838 | 13, 14, 17–19 |
| 13 | 4961–5064 | shared unwrapping projections: `as_record_decl`, `build_spec_item` | 104 | 15–19 |
| 14 | 5065–5387 | in-class member collection | 323 | 17, 19 |
| 15 | 5388–6203 | coverage roster and pre-pass sets: `build_roster`, `build_expos_set` | 816 | 17–19 |
| 16 | 6204–6470 | what a non-extracted body names | 267 | 18, 19 |
| 17 | 6471–7342 | `classify()` | 872 | 18, 19 |
| 18 | 7343–7591 | source files (issue #149) | 249 | 19 |
| 19 | 7592–8153 | entry points: `collect_interleaved`, `build_document` | 563 | — |

Two sections need almost no Clang. Token rewriting via sentinels uses Clang on one line, and that line is
`format_and_recover` calling the formatter. Normalization uses only `clang::format`, not the AST. Every other section
passes `clang::Decl*`, `clang::SourceManager&` or `clang::ASTContext&` across its boundary.

The interface surface comes straight from the "used by" column: a name another section
uses becomes a declaration in that section's header, and everything else stays file-local in its `.cpp`. The
docblock-and-edits section exports the most, about fifteen names. Most sections export two to six.

## How each step is done

- **Move verbatim.** Code moves from `frontend.cpp` into the new `.cpp` unchanged except for the namespace it now
  lives in. Review it with `git diff --color-moved`. Rewrites, renames and fixes go in separate commits, or in
  later PRs.
- **Namespace follows directory.** A component in `frontend/` is in namespace `beman::specgen::frontend`. A name
  that leaves the anonymous namespace gains external linkage, so a generic helper name (`is_ident_char`) is
  checked for collisions in that step, and renamed only if it collides.
- **One header, one facility, one test file.** Each new header gets `<name>.test.cpp` with the double include
  and a bootstrap case. It gets direct cases where the claim is cheap to state; see
  [frontend-component-tests](#frontend-component-tests).
- **Green at every step.** Full ctest including the three style gates, and `make lint`. Each PR bumps the patch
  version once, in its last commit, and references #187.
- **The file shrinks from the bottom of the DAG up.** A component can depend only on components already extracted,
  so the order follows the table, top to bottom, except where a step below says otherwise.

## Steps

The stages are numbered for reading order only. Cross-reference them by slug.

### [entry-point-headers](#entry-point-headers)

*Stage 1.* **Status:** DONE, 2026-10-25.

`smoke_check` and `SmokeResult` move to `frontend/smoke_check.hpp`/`.cpp`. `install_crash_handler` moves to
`frontend/crash_handler.hpp`/`.cpp`. Neither depends on anything else in the file. The `smoke_check` cases in
`frontend.test.cpp` move to `smoke_check.test.cpp`, and the `build_document_with_sources` cases stay.
`tools/specgen/main.cpp` includes the two headers directly.
This needs no decision from the open questions below, because neither header carries a Clang type.

### [string-level-pieces](#string-level-pieces)

*Stage 2.* **Status:** DONE, 2026-10-25.

The two sections that barely touch Clang, with direct tests on strings:

- **Sentinels.** `SpanInfo`, `span_sentinel`, `recover_sentinels`, `exact_width_sentinel`, `resize_sentinels` and
  `has_lone_semicolon` need only `ir`. They move to the core library; see [sentinel-home](#sentinel-home).
- **Draft formatting.** `draft_format_style`, `format_code`, `format_draft` and `format_and_recover` move to
  `frontend/draft_format.hpp`. `kDraftColumnLimit` and `kNamespaceIndent` go with them. The Constraints
  derivation builds its own variant of the style (line 3564), so the step decides whether the header exposes
  `clang::format::FormatStyle` or names the two styles as functions over strings. The second keeps Clang out of
  the interface, the way `compile_args.hpp` does, and is preferred if the Constraints variant fits it.

**Log:** 2026-10-25: done as `span_sentinels.hpp` in the core and `frontend/draft_format.hpp`. The Constraints
variant fit the string interface: `qualifier_style` and `format_expr_fragment` moved into `draft_format.cpp`, so
the header exposes `format_draft`, `format_and_recover` and `format_expr_fragment`, and no `FormatStyle`.
`frontend.cpp` no longer includes `clang/Format/Format.h`. Both components have direct tests on strings.

### [parse-header](#parse-header)

*Stage 3.* **Status:** DONE, 2026-10-25.

The prelude's parse path and the file helpers: `ParsedHeader`, `parse_header`, `parse_sources`, the resource-dir
handling, `DocumentFiles`, decl collection, `file_buffer`, `file_comments`, skipped ranges and
`diagnostic_file_name`. This is the first step whose interface carries Clang types, so
[clang-header-home](#clang-header-home) must be settled first.

`collect_interleaved`, `SourceItem` and `InterleaveResult` then move to `frontend/interleave.hpp`, and
`collect.test.cpp` becomes `interleave.test.cpp`. `parse_header` gains a from-text overload if the step needs one
for tests; it becomes the fixture later component tests build their ASTs with.

**Log:** 2026-10-25: done as seven components. The table's row 0 hid one exception to "every use points
backwards": `DocumentFiles::discover` is defined after `classify()` and reads `section_end_fence`, which lived there.
The fence scan, and `parse_ref` beside it, are Clang-free, so they went to the core as `section_markers.hpp`,
namespace `grammar`. `BuildFailure` moved below the entry points to `frontend/build_failure.hpp`, so
`interleave.hpp` names it without including `frontend.hpp`. The rest:

- `frontend/parse_header.hpp`: `ParsedHeader`, `parse_header`, `parse_sources`, `unparsed_header`
- `frontend/document_files.hpp`: `DocumentFiles`, `document_line`, the two top-level collections
- `frontend/file_views.hpp`: `file_buffer`, `file_comments`, `diagnostic_file_name`, skipped ranges
- `frontend/decl_projections.hpp`: `as_out_of_line_member`, `function_or_template`, `is_first_declaration`. The
  shared unwrapping projections of [body-extraction](#body-extraction) can join it.
- `frontend/interleave.hpp`: `SourceItem`, `InterleaveResult`, `collect_interleaved`. `collect.test.cpp` is now
  `interleave.test.cpp`.

`frontend.cpp` no longer includes `clang/Tooling/Tooling.h`. It is down to about 7100 lines.

### [qualifiers-and-edits](#qualifiers-and-edits)

*Stage 4.* **Status:** DONE, 2026-10-25.

The reference-resolved namespace mapping becomes `frontend/qualifiers.hpp`. The docblock-lookup-and-edits section
splits in two:

- **Docblock lookup:** `attached_raw_comment`, `has_docblock`, `docblock_directives`, `docblock_diagnostics` and
  `record_docblock`.
- **Synopsis edits:** `SynopsisEdit`, `add_dominant_edit`, `sort_back_to_front`, `edits_overlap` and the masks.
  `SynopsisEdit` holds absolute offsets. If the edit algebra turns out to be Clang-free, it drops into the core
  like the sentinels do.

**Log:** 2026-10-25: done as four components. The edit algebra (`SynopsisEdit`, `sort_back_to_front`,
`edits_overlap`, `add_dominant_edit`) is Clang-free and went to the core as `synopsis_edits.hpp`. The masks
(`SeeBelowTarget` and its enumerator list, the alias and definition masks, `seebelow_source_range`) became
`frontend/declaration_masks.hpp` rather than staying with the docblock lookup. `expos_uses` was a template over a
dozen node types, which would have put its visitor in the header; the header declares two overloads instead,
`clang::Decl*` and `clang::Stmt*`, which is what the template's `if constexpr` dispatched on, and the visitors stay
in `qualifiers.cpp`. `frontend.cpp` no longer uses `foundation::` at all, and is about 6350 lines.

### [synopsis-extraction](#synopsis-extraction)

*Stage 5.* **Status:** DONE, 2026-10-25.

The exposition-only uses section, then lines 2118–3268. That range is two topics under one banner: merged twins
(2118–2274), and synopsis extraction (2275 onward), which the twins feed. They become two components. At about
1550 lines, this is the largest step.

**Log:** 2026-10-25: done as five components. The exposition-only uses section held two topics, so it became
`frontend/record_members.hpp` (the member projections) and `frontend/expos_spellings.hpp` (the name-match
rewrites). The merged twins forward-declared `split_conjuncts`, which was defined later, in the Constraints
section, and the three requires-clause accessors sat between the twins and extraction under extraction's doc
comment. All five now form `frontend/requires_clauses.hpp`, below the twins, extraction, and the derivations that
come next. The rest are `frontend/merged_twins.hpp` and `frontend/synopsis_extraction.hpp`. Default arguments stay
on the header declarations. `frontend.cpp` is about 4770 lines.

### [itemdecl-and-derivations](#itemdecl-and-derivations)

*Stage 6.* **Status:** DONE, 2026-10-25.

Redeclaration-chain attachment (`extract_itemdecl`), then the three derivations as one component each: Constraints,
Mandates, and the class-head `static_assert`s. Mandates and the class-head pass both use `phrase_conjunct`, so
Constraints goes first.

**Log:** 2026-10-25: done as four components, one per section: `frontend/itemdecl.hpp`,
`frontend/constraints.hpp`, `frontend/mandates.hpp`, and `frontend/class_head_mandates.hpp` (not
`class_description`, a name a scenario test already holds). `constraints.test.cpp` and `mandates.test.cpp` were
already the scenario tests for those derivations, so they became the components' test files. `split_conjuncts`
had gone to `requires_clauses` in [synopsis-extraction](#synopsis-extraction). `frontend.cpp` is about 3970
lines.

### [body-extraction](#body-extraction)

*Stage 7.* **Status:** DONE, 2026-10-25.

`\effects-equiv` / `\returns-expr` body extraction with its `attach_*` family, the shared unwrapping projections,
and in-class member collection. The projections section uses `attach_function`, and `build_spec_item` lives there,
so the step decides whether the projections stand alone or join body extraction.

**Log:** 2026-10-25: done as two components, and four projections that joined `decl_projections`. The body
extraction section is really attachment: `AttachedItem` and the `attach_*` family, with body extraction as their
file-local helpers, so it is `frontend/attach.hpp`. `build_spec_item` calls `attach_function`, so it went there
rather than to the projections. In-class collection is `frontend/inclass_items.hpp`. `ClassRefGroups` had its
member functions defined in the class body; they are defined out of line in `inclass_items.cpp` instead, so the
header carries only the declarations. `attach.test.cpp` was already attachment's scenario test and became the
component's test file. `frontend.cpp` is about 2710 lines.

### [coverage-roster](#coverage-roster)

*Stage 8.*

The coverage roster and the pre-pass sets (`build_roster`, `build_expos_set`, `build_omit_set`, the foreign
collections). Also what a non-extracted body names (`body_member_uses`, `collect_unextracted_uses`).

### [classify-and-sources](#classify-and-sources)

*Stage 9.*

`classify()` and the source-file harvest. The harvest loop becomes a `fold_left_short` call in this step (#196).
What is left in `frontend.cpp` is the entry points, about 560 lines; [frontend-hpp-name](#frontend-hpp-name)
decides what the header and file are called after that. The step closes
[frontend-header-facilities](header-discipline-review.md#frontend-header-facilities) and records the final
component list in `docs/architecture.md` §11.

## Open questions

### [clang-header-home](#clang-header-home)

**Question:** where does a header whose interface carries Clang types live, and how is it checked? **Status:**
DECIDED, 2026-10-25. **Decided by:** Steve Downey.

`CMAKE_VERIFY_INTERFACE_HEADER_SETS` compiles each FILE_SET header against the target's *interface*. The front-end
target takes the Clang include directories `PRIVATE`, so a FILE_SET header that includes `<clang/AST/Decl.h>`
fails verification. The three style gates find headers through those verification TUs, so a header outside a
FILE_SET is invisible to them.

- **In the FILE_SET, Clang includes public.** The front-end target takes the Clang include directories as `PUBLIC
  SYSTEM`, and the headers go in `include/beman/specgen/frontend/` like any other. Every header is verified and
  gated. The cost is that everything linking the front end sees Clang's headers, which today means the driver and
  the tests. specgen is a tool, not a library (decision `tool-not-library`), so no consumer outside the tree is
  affected.
- **Private headers beside the sources.** The headers go in `src/beman/specgen/frontend/`, outside any FILE_SET.
  Nothing verifies them on their own, and the gates see them only through the `.cpp` files that include them.
  This breaks the header-discipline rule that every header compiles standalone, and that is a checked rule.

Recommendation: the first. Its cost is mostly theoretical, while the second gives up a gate.

**Decision:** the first. The front-end target takes the Clang include directories as `PUBLIC SYSTEM`, and its
component headers go in `include/beman/specgen/frontend/` like any other.

### [sentinel-home](#sentinel-home)

**Question:** where do the sentinel helpers go? **Status:** DECIDED, 2026-10-25. **Decided by:** Steve Downey.

They need only `ir`, so the DAG puts them in the core library, beside `conjuncts` and `lower`, whatever their only
caller is.

**Decision:** `span_sentinels.hpp` in the core, tested directly on strings. Its namespace is
`beman::specgen::span_sentinels`, after the header, as `conjuncts.hpp`'s is `conjuncts`.

### [frontend-component-tests](#frontend-component-tests)

**Question:** how much direct testing does an extracted AST component get? **Status:** OPEN.

The scenario test files already cover these components through `build_document`, and they stay as they are.
A component whose interface takes a `clang::Decl*` can be tested directly only from a parsed snippet.

Recommendation: every new header gets its test file with the double include and the bootstrap case, which is what
the header rule requires. Direct cases are added where a snippet states the claim more clearly than a corpus
header does, using `parse_header` from [parse-header](#parse-header) as the fixture. They are not a gate on
each step: a verbatim move does not need a new test to prove it moved.

### [frontend-hpp-name](#frontend-hpp-name)

**Question:** once the rest has moved out, what is `frontend.hpp` called? **Status:** OPEN.

What it will hold is `build_document`, `build_document_with_sources`, `BuildFailure`, `DocumentBuild` and
`SourceFile`: one facility, building a document from a header. That suggests `frontend/build_document.hpp`. The
name `frontend.hpp` describes a tier, which is the kind of name this review has been moving away from. Every
scenario test includes it, so the rename is mechanical but touches about 60 files.
