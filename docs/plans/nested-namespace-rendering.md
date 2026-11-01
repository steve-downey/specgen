# Plan: render nested interface namespaces

**Status:** DONE, 2026-10-31. Every recommendation below was accepted as written and every step is
implemented on the branch this plan landed with; the decision is recorded in
[decisions/nested-namespace-rendering.md](../decisions/nested-namespace-rendering.md). Two refinements over
the plan as proposed: a namespace docblock *without* `\specified` is not the namespace's (Clang attaches a
detached `\verbatim-itemdecl` block above a `namespace` line to it, and `spec_verbatim_record_merge.hpp`
relies on that), so only a docblock carrying the marker is checked for extras; and an index form stays the
open question it was.

Issue [#209](https://github.com/steve-downey/specgen/issues/209). A header whose public names sit in a nested
namespace (`beman::transcoding::views::null_term`, `beman::transcoding::whatwg::decoder<E>`) loses that
namespace in the wording: every declaration renders directly inside `namespace std {`, so the paper proposes
`std::null_term` when it means `std::views::null_term`, and a `whatwg::decode<E>` variable template lands beside
a `decode(d)` function template as siblings, which is not the interface and is not valid C++.

## Why it happens

The front end has a two-valued model of namespaces (design §3.5):

- **Dropped.** `std` plus the header's own top-level namespaces, prefix-minimal
  (`build_namespace_drop_set`, `frontend/directive_sets.cpp`). A qualifier resolving into this set is deleted,
  and `enclose_in_namespace_std` (`frontend/header_synopsis.cpp`) hard-codes the one enclosure every synopsis
  gets.
- **Foreign.** Everything else. A qualifier naming one survives verbatim and is recorded as
  `ir::ForeignNamespace` (`collect_foreign_namespaces`, `frontend/foreign_names.cpp`); the validator reports
  each `name::` occurrence as an Error (§9). The one exception is a namespace rooted in `std`
  (`std::ranges::`), which is the standard's own vocabulary and renders silently.

A nested interface namespace is neither. Its declarations flatten because `collect_top_level_decl`
(`frontend/document_files.cpp`) descends through every `NamespaceDecl` without recording it, and nothing
downstream knows which namespace a declaration came from. A written `views::x` in rendered code survives the
drop set and is then an Error, exactly as `detail::x` is, because the tool cannot tell the two apart. There is no
AST property that distinguishes them: the difference is authorial intent, so it needs a marker.

## Design

A third class, **specified** namespaces, decided in the front end. The IR does not change: the synopsis text
carries the namespace blocks, the itemdecl text carries the qualified declarator, and `foreign_namespaces`
simply omits the specified ones. `generate` and `--emit-ir | render --from-ir` therefore agree by construction.

### Which namespaces are specified

A nested namespace below the document's own namespace is specified when either

- its qualified path is rooted in `std` (`std::ranges`, `std::chrono`): the guard
  `ForeignQualifierCollector::record` already applies, generalized; or
- some redeclaration of it, anywhere in the translation unit, carries `//! \specified` as its docblock.

Clang attaches a `//!` comment to a `NamespaceDecl` (verified against the pinned clang 23 with `-ast-dump`), so
`docblock_directives` reads it unchanged. The marker is looked up across `ns->redecls()`, so a project marks the
namespace once, in a header every other header includes, and every document in the paper sees it; this mirrors
how `\expos` reaches an included header (issue #36).

The **rendered path** of a declaration is the longest prefix of its namespace path, relative to the drop-set
prefix, every component of which is specified. `beman::transcoding::views::null_term` renders in `views`;
`beman::transcoding::whatwg::detail::helper` renders in `whatwg` (its `detail` flattens into the nearest
specified ancestor, exactly as an unmarked `detail` flattens into `std` today); `beman::transcoding::decode`
renders at the top, as now. Nothing changes for a header that marks nothing: every existing golden must come
out byte-identical, which is the acceptance test for the default.

Opt-in rather than opt-out, because the opposite default silently withdraws the leakage Error from every
existing user's `detail::` the day they upgrade. A command-line list was rejected for the reason every other
marker lives in the header: the source is the specification, and a flag is not reviewed with it.

### Synopsis shape

- **Gathered header synopsis** (`.syn` region, the fold in `build_document.cpp`). The fold tracks the rendered
  path of the declaration being appended. When it changes, the open block closes and a new one opens; the
  block's body is indented one more level. This is [ranges.syn]'s `namespace views { ... }` inside
  `namespace std::ranges {`, and [time.syn]'s re-opening of blocks in source order. A `\ref` group header
  comment is deferred to the run of the declaration that follows it, which is where the draft writes it. A
  multi-component rendered path emits one `namespace a::b {` block (the draft's own nested-namespace-definition
  spelling, as in `namespace std::chrono {`). An inline namespace keeps its `inline`.
- **Standalone class synopsis.** `db::SynopsisDecl` gains a rendered-path field, set by classify's record arms
  and the namespace-expos arm. The final enclosure loop in `build_document.cpp` writes
  `namespace std::whatwg {` instead of `namespace std {`, which is [fs.class.path]'s
  `namespace std::filesystem { class path { ... }; }` and [time.duration.general]'s shape. An attached
  `\verbatim-synopsis` takes the same enclosure; a detached one stays exactly as written (§3.5, unchanged).
- **Formatting width.** `format_and_recover` already takes the indent the text will receive afterwards
  (`kNamespaceIndent`, issue #157). A declaration inside a nested block passes `kNamespaceIndent * (1 + depth)`
  so the final text still respects the draft's 88 columns.
- `enclose_in_namespace_std` becomes `enclose_in_namespace(code, "std::whatwg")` or
  `enclose_in_namespace(code, "views")`; the offset remapping it does is the same for any name.

### Itemdecl shape

A namespace-scope entity in a specified namespace renders its declarator-id qualified by the rendered path:
`constexpr decoder<E> whatwg::decode(...)`, `inline constexpr unspecified views::null_term;`. This is the
draft's `ranges::copy` ([alg.copy]) and `filesystem::absolute` ([fs.op.absolute]). The insertion goes at the
declarator name token, whose range `extract_header_declaration` and `extract_freestanding_declaration` already
compute for the index sentinel, as one more `SynopsisEdit`. An out-of-line member of a class in a specified
namespace is not qualified: its class synopsis already sits in `namespace std::whatwg {`, and the draft writes
`path& operator/=(const path&);` bare under it.

Index entries and the `entities` channel do not change in this plan. The draft is inconsistent here
(`\indexlibraryglobal{copy}` for `ranges::copy`, `\indexlibrarymember{filter}{views}` for `views::filter`);
see [index-form-for-nested-namespace](#index-form-for-nested-namespace).

### Qualifiers and the leakage checker

`namespace_qualifier_edits` does not change. A written `views::x` is not in the drop set and survives; a written
`beman::transcoding::views::x` loses `beman::transcoding::` through the existing prefix walk and leaves
`views::x`, so both spellings of one use render the same way, which is the property the drop set's
prefix-minimality was built for.

`collect_foreign_namespaces` skips a specified namespace the way it skips a `std`-rooted one, so the validator
never sees `views` in `foreign` and the Error is gone. The checker's guarantee for an unmarked `detail::`
stands exactly as before. A declaration inside a specified namespace lives in the document, so the bare-name
clause (`ForeignDeclaration`) is unaffected.

### The marker itself

- One row in `kMarkers` (`markers.hpp`), one field in `Markers`, arity `Flag`.
- `//! \specified` on anything that is not a namespace is an Error, like `\verbatim-synopsis` on a non-class.
  Any other marker on a namespace's docblock is an Error too: a namespace has no wording of its own.
- A namespace is never a `RawItem`, so its docblock comment reaches `classify` as a bare markup comment, which
  today returns `Ignored{}` silently. It stays that way; the pre-pass is where the marker is read.

## Open questions

Slug-headed so the record can graduate in place. Each is a taste call the user owns; the recommendation is
first.

### marker-spelling

**Question.** What is the marker on the namespace called?
**Status.** OPEN.
**Options.** `\specified` (the issue's suggestion; "this namespace is part of what is specified", reads
beside `\expos` and `\elsewhere` as the third visibility statement) — recommended. Alternatives considered:
`\interface`, `\wording`. Not `\expos`, which already means exposition-only and would say the opposite.

### opt-in-default

**Question.** Does an unmarked nested namespace render or flatten?
**Status.** OPEN.
**Recommendation.** Flatten, as today. See "Which namespaces are specified" for why opt-out withdraws the
leakage Error silently. The cost is one marker per interface namespace, written once per project.

### nested-path-block-form

**Question.** For a rendered path of two or more components inside a gathered synopsis, one
`namespace a::b {` block or nested single-component blocks?
**Status.** OPEN.
**Recommendation.** One block. The draft uses the nested-namespace-definition spelling for its own nested
namespaces and it costs one indent level instead of two.

### variable-itemdecl-qualification

**Question.** `inline constexpr unspecified views::null_term;` as a described range adaptor object's itemdecl:
accept the qualified declarator, or treat variables differently?
**Status.** OPEN.
**Recommendation.** Accept. The draft never writes an itemdecl for a `views::` object (it describes it in prose
under the synopsis), so there is no precedent to break, and one rule for every namespace-scope entity is easier
to state than two. A qualified declarator on a variable is a legal out-of-line definition.

### index-form-for-nested-namespace

**Question.** Should a nested-namespace entity index as `\indexlibrarymember{name}{views}` rather than
`\indexlibraryglobal{name}`?
**Status.** OPEN, deferred: not in this plan's PR.
**Recommendation.** Keep `Global` now and file a follow-up issue. The draft itself does both, and the index
form is backend metadata that can move without touching the text this plan settles.

### version-bump-level

**Question.** Patch or minor?
**Status.** OPEN.
**Recommendation.** Minor: a new marker and a new output shape, with no change to unmarked input.

## Steps

One PR, in this commit order; every commit green (full ctest, `make lint`). It splits cleanly after step 4 if
review wants the itemdecl half separately.

1. **Marker.** `Markers::specified`, the `kMarkers` row, `markers.test.cpp` and `docblock.test.cpp` cases for
   parse and duplicate detection. No behavior yet.
2. **Pre-pass.** `build_specified_namespace_set(decls, sm)` in `frontend/directive_sets` returns the qualified
   paths of every namespace some redeclaration marks, plus a `rendered_namespace_path(decl, drop_set,
   specified)` projection (relative path, longest all-specified prefix, `std`-rooted counts as specified).
   `namespace_path` leaves the anonymous namespace for it. Diagnostics for `\specified` off a namespace and
   for any other marker on one. Unit tests in `directive_sets.test.cpp`.
3. **Leakage.** `collect_foreign_namespaces` takes the specified set and skips its members.
   `foreign_names.test.cpp` pins that `views` is not recorded and `detail` still is.
4. **Synopsis.** `enclose_in_namespace(code, name)` replaces `enclose_in_namespace_std`; `db::SynopsisDecl`
   gains the rendered path; classify sets it; the gathered fold opens and closes blocks by rendered path and
   defers group headers; `extract_header_declaration` and `extract_synopsis` format at the depth-adjusted
   indent. `header_synopsis.test.cpp` covers span remapping at depth 2; `build_document.test.cpp` covers the
   open/close sequence over synthetic events.
5. **Itemdecl.** The declarator-qualification edit in `extract_header_declaration` (for the folded-in
   declaration path) and `extract_freestanding_declaration` / `extract_itemdecl` (for documented
   namespace-scope entities and functions). `itemdecl.test.cpp` cases.
6. **Corpus and goldens.** `tests/corpus/spec_nested_namespace.hpp`, document namespace `demo`, holding: a
   `//! \specified namespace views` with a masked adaptor object described by `\remarks`; a
   `//! \specified namespace proto` with a scoped enum, a class template, and a function template `decode`,
   beside a same-named `decode` one level up (the issue's hazard, now in different blocks); a `std::ranges`
   specialization with no marker; an unmarked `detail` with an `\omit`; a use written `views::adaptor` and one
   written `demo::views::adaptor`; everything inside one `.syn` region; `\rSec` sections with out-of-line
   definitions so the itemdecls show `proto::decode` beside `decode`. Registered in generate mode with its
   `.validate` sibling demanding silence (the `views::` qualifier is the point), and render-mode goldens for
   all three backends from its IR. A diagnostics golden for `\specified` on a function. The acceptance check
   for the default: every existing golden and every `examples/cli` capture unchanged; if a capture moves, the
   default moved and the step is wrong.
7. **Docs and version.** `docs/architecture.md` §3.5 (the "every synopsis is enclosed in `namespace std`"
   bullet and the namespace-mapping bullet), §4.3 marker list, §9 qualifier clause (a specified namespace is
   not foreign), §11 inventory; `docs/user-guide.md` marker list and the "header's own namespace is dropped"
   paragraph; a decision record `docs/decisions/nested-namespace-rendering.md` and its README row; the four
   version files, in the last commit.

## Out of scope

- Moving an implementation-namespace declaration *out* of a gathered synopsis. Today an unmarked `detail`
  declaration inside a `.syn` region flattens unless `\omit`ted (`spec_header_synopsis.hpp` relies on this);
  that is the same behavior under a specified ancestor and is a separate question.
- The index form ([index-form-for-nested-namespace](#index-form-for-nested-namespace)).
- Prose: `views::null_term` in a `\remarks` is authored text and already renders as written.
