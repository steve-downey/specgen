# Review: header discipline, coupling, and test mapping

A point-in-time review of the 25 headers under `include/beman/specgen/` and the 39 test files under
`tests/beman/specgen/` against these rules:

1. Every header compiles on its own and is idempotent (may be included twice), and that is formally checked.
2. A header provides a single facility, and is named for it.
3. Each facility's tests live in a distinct test file named for the header, and test that facility, not its
   dependencies.
4. A header includes its own direct dependencies.

Rules 1 and 4 were checked mechanically; rules 2 and 3 by reading. The mechanical checks are reproducible with the
commands in the companion plan, [no-raw-loops-tidy-plugin](no-raw-loops-tidy-plugin.md), which puts the same
header-as-TU inventory behind a gate.

## Mechanical results

| Check | Method | Result |
| --- | --- | --- |
| Compiles standalone | one CMake verify TU per header, parsed by clang-tidy | 26 of 26 clean |
| Idempotent | header included twice, `g++ -fsyntax-only` with the verify TU's flags | 25 of 25 clean |
| Direct dependencies included | `misc-include-cleaner`, header TUs and all production `.cpp` | 0 in headers |
| Re-inclusion in the header's own test | the header included twice in its `.test.cpp` | 21 of 39 test files |

Every header has an include guard. The tree already has a re-inclusion convention: 21 test files include their
header twice, most with a `// Re-inclusion verification` comment. Where it is absent:

- `document_build.test.cpp` includes its header once. It is the only tested header without the check.
- The 17 frontend slice tests include `frontend/frontend.hpp` once each; `frontend/frontend.test.cpp` does it
  twice, so the header is covered.
- `specgen.hpp`, `config.hpp`, and `diagnostic.hpp` have no test at all, so neither formal check runs for them
  today. They passed the mechanical checks above, which is what turning `VERIFY_INTERFACE_HEADER_SETS` on in the
  build would keep true without a test per header.

The production `.cpp` files are a different story on rule 4: `misc-include-cleaner` reports 183 sites that reach
a symbol through a transitive include, concentrated in `validate.cpp`, `lower.cpp`, `frontend.cpp`, and
`tools/specgen/main.cpp`. Not in scope for the header rule, but it is the backlog that stands between the check and
being a gate.

## Facility and test map

| header | facility | single facility? | name fits? | test file |
| --- | --- | --- | --- | --- |
| `backend/common.hpp` | shared backend substrate | no, four | layer name | `backend/common.test.cpp`, partial |
| `backend/latex.hpp` | draft-LaTeX renderer | yes | yes | `backend/latex.test.cpp` |
| `backend/mpark.hpp` | mpark/wg21 renderer | yes | yes | `backend/mpark.test.cpp` |
| `backend/org.hpp` | org-mode renderer | yes | yes | `backend/org.test.cpp` |
| `config.hpp` | build-config macro shim | yes | yes | none |
| `conjuncts.hpp` | conjunction-to-wording layout | yes | yes | `conjuncts.test.cpp` |
| `diagnostic.hpp` | severity vocabulary and label | yes | yes | none |
| `docblock.hpp` | docblock grammar and parser | no, also the marker registry | broad | `docblock.test.cpp` |
| `document_build.hpp` | clang-free document-tree build | mostly, by decision | yes | `document_build.test.cpp` |
| `foundation/fold_left_short.hpp` | short-circuiting fold | yes | yes | `foundation/fold_left_short.test.cpp` |
| `foundation/json_descriptor.hpp` | descriptor-driven JSON | no, also a JSON lexer | narrower | its `.test.cpp` |
| `foundation/json_writer.hpp` | JSON punctuation scopes and escaping | yes | yes | `foundation/json_writer.test.cpp` |
| `foundation/monoid.hpp` | monoid, `mconcat` | yes | yes | `foundation/monoid.test.cpp` |
| `foundation/overloaded.hpp` | exhaustive visitor | yes | yes | `foundation/overloaded.test.cpp` |
| `foundation/parse/cursor.hpp` | input cursor and position | yes | yes | `foundation/parse/cursor.test.cpp` |
| `foundation/parse/parser.hpp` | parser combinators | yes | yes | `foundation/parse/parser.test.cpp` |
| `foundation/traverse.hpp` | `sequence`/`traverse` over `expected` | yes | yes | `foundation/traverse.test.cpp` |
| `fragments.hpp` | document splitting | yes, plus a file-name check | yes | `fragments.test.cpp` |
| `frontend/frontend.hpp` | the Clang tier | no, five (see finding) | tier name | 18 files under `frontend/` |
| `ir.hpp` | wording IR | no, also JSON emit and parse | says types | `ir.test.cpp` |
| `ir_fold.hpp` | `Node` base functor | yes | yes | `ir_fold.test.cpp` |
| `lower.hpp` | docblock-to-IR lowering | mostly, plus `exposid_name` | yes | `lower.test.cpp` |
| `markers.hpp` | marker struct | yes; its registry is in `docblock.hpp` | yes | via `docblock.test.cpp` |
| `specgen.hpp` | umbrella | umbrella | yes | none |
| `validate/validate.hpp` | IR validators | yes | yes | `validate/validate.test.cpp` |

## Findings

Ordered by how much they cut against the rules. Each is a question with a slug so later work can link to it; none
is decided here.

### [frontend-header-facilities](#frontend-header-facilities)

**Question:** which facilities in `frontend/frontend.hpp` belong in headers of their own? **Status:** DECIDED for
the compile-argument handling and `parse_rsec`, 2026-10-24; OPEN for the rest.
The header bundles at least five: `smoke_check` (a link probe), `collect_interleaved` and `SourceItem`,
`ParseOptions` with `filter_compile_command_args` and `resolve_extra_args` (pure string handling, no Clang), the
`parse_rsec` combinator parser, and `build_document`. The header's own comments call two of them out as
Clang-free and exposed for testability. The 18 test files are the visible symptom: `rsec.test.cpp`,
`collect.test.cpp`, and `frontend.test.cpp` each test a distinct facility, while the other 15 slice `build_document`
by corpus feature. That last group is scenario slicing of one facility, which the rule allows; the first three are
facilities without a header. Splitting touches the tier boundary described by decision `ir-boundary`, so it is a
design change, not a move.

**Decision:** the tier split is not itself a constraint. Front end, IR, and backend have been a useful shape, but
they are an artifact of how the tool was built; what matters is that components form a DAG. Code the tiers would
share drops down a level, to `foundation/` or to a new level that needs no name. So:

- `frontend/compile_args.hpp` takes `ParseOptions`, `filter_compile_command_args`, `ResolvedArgs`, and
  `resolve_extra_args`. It stays in the front-end library, because `resolve_extra_args` loads a Clang compilation
  database, but its interface is plain strings. Its tests are `frontend/compile_args.test.cpp`.
- `rsec.hpp` takes `SectionHeader`, `parse_rsec`, and `rsec_tag_recognized`, in namespace `grammar` beside the
  docblock grammar. It needs no Clang, so it moves into the core library, and its tests move up from `frontend/`.
- `smoke_check`, `install_crash_handler`, `collect_interleaved`, and `build_document` stay in `frontend.hpp` for
  now. Splitting them is the open part of this question, and the cost there is `frontend.cpp`, not the header.

**Decided by:** Steve Downey.

**Log:** 2026-10-24: the draft-heading heuristic (`unrecognized_section_header`) stays in `frontend.cpp`: it reads
comment vocabulary through an LLVM string type. `resolve_extra_args` gained direct tests, including one against a
real `compile_commands.json`.

### [backend-common-facilities](#backend-common-facilities)

**Question:** what are the facilities in `backend/common.hpp`, and which backends consume each? **Status:** DONE,
2026-10-24.
Four live there with different consumer sets: the element-label table (mpark, org), `render_code_spans` (all),
the draft-LaTeX span spellings (latex, org), and the `RenderF` base functor. `common.test.cpp` tests only
`render_code_spans`; the label table is asserted from `mpark.test.cpp` and `org.test.cpp` instead, and the span
spellings from `org.test.cpp`. The name says "shared", which is a layer, not a facility.

**Decision:** each facility moves out of `common.hpp` into a header-only component of its own, below the backends
that use it, and `common.hpp` is deleted. They stay in `backend/`, in namespace `backend`, because the backends are
their only consumers:

- `backend/element_label.hpp`: `kElementLabels` and `element_label`, on `ir`. Used by mpark and org.
- `backend/code_spans.hpp`: `render_code_spans`, on `ir`. Used by all three.
- `backend/draft_spans.hpp`: `draft_span_prose`, `draft_span_codeblock`, and `draft_code_inline`, on `code_spans`
  and `ir`. Used by latex and org.
- `backend/render_fold.hpp`: `RenderedSectionF`, `RenderF`, and `render_fmap`, on `ir` and `foundation/overloaded`.
  Used by all three.

Each has its own test file, split from `common.test.cpp`. `render_fmap`, which had no test, gets one, and
`element_label` gains a check that every element kind has a label. The per-backend label cases in `mpark.test.cpp`
and `org.test.cpp` stay, as [tests-of-dependencies](#tests-of-dependencies) decided. **Decided by:** Steve Downey.

### [ir-serialization-home](#ir-serialization-home)

**Question:** does JSON emit and parse belong in `ir.hpp`? **Status:** DONE, 2026-10-24. The header carries the node types,
`canonicalize`, the name tables, and the JSON contract with three deprecated shims. Decision `node-base-functor`
already chose to keep `ir.hpp` byte-identical and put the functor beside it in `ir_fold.hpp`, which is the pressure
this bundling creates. `ir.test.cpp` and `ir_fold.test.cpp` both pin the JSON schema, the second one by
re-implementing emission through `json_writer`, and the file's own comment acknowledges the duplication.

**Decision:** no. The IR's JSON format becomes `ir_json.hpp`/`ir_json.cpp`, in namespace `ir` on the `ir_fold.hpp`
precedent: `emit_json`, `ParseError`, `parse_document`/`parse_item`/`parse_code`, and the descriptor table for each IR
type. It sits above `ir` and the generic `json/` components ([json-reader-home](#json-reader-home)), and nothing in
the pipeline depends on it: the front end, backends, and validators never serialize; the driver and the examples do.
`ir.hpp` keeps the node types, `canonicalize`, and the enumeration name tables, which the LaTeX backend and the
docblock grammar read as well. The deprecated `parse_json_*` shims, whose only caller was their own test, are
deleted rather than moved. **Decided by:** Steve Downey.

**Log:** 2026-10-24: `span_kind_name` and `index_kind_name` (and their reverse lookups) were private to `ir.cpp`;
they are now declared in `ir.hpp` beside their enumerations, like the other three name tables, so `ir_json.cpp` can
read them. The JSON cases move from `ir.test.cpp` to `ir_json.test.cpp`. `ir_fold.test.cpp` keeps its emission
algebra, which is the proof obligation decision `node-base-functor` set for the fold, pinned against `emit_json`.

### [json-reader-home](#json-reader-home)

**Question:** is the `Reader` lexer in `foundation/json_descriptor.hpp` a facility of its own? **Status:** DONE,
2026-10-24. It is about 240 lines of hand-written JSON lexing inside a header named for descriptors; the head comment
rationalizes the co-location. `json_writer.hpp` is the precedent for the other direction.

**Decision:** yes, and the JSON code is two things, not one: an IR-agnostic JSON library and the IR's own format
built on it ([ir-serialization-home](#ir-serialization-home)). The library gets a directory of its own, `json/`,
namespace `json`, as three components: `writer.hpp` (the old `json_writer.hpp`), `reader.hpp` (the `Reader`), and
`descriptor.hpp` (the schema engine, which uses both). The writer and the reader stay apart, since neither calls the
other. It leaves `foundation/` because `foundation` is a name for things without a better one, and for imports being
evaluated for upstream feedback; a sub-library with a name of its own does not belong there. **Decided by:** Steve
Downey.

**Log:** 2026-10-24: `reader.test.cpp` gives the `Reader` direct tests: string escapes and `\u` decoding, numbers
and booleans, object and array callbacks, `skip_value`, and that the first failure is the one recorded. Identifiers
keep their names (`json_object`, `json_descriptor`), so the move renames nothing but namespaces and paths.

### [marker-registry-home](#marker-registry-home)

**Question:** should the marker registry sit with the marker struct? **Status:** DONE, 2026-10-24. `markers.hpp`
holds the struct and claims a single definition; the table, arity enum, and setters live in `docblock.hpp`, and the
only test of the struct is inside `docblock.test.cpp`. Decision `marker-registry` names one table, not one header.

**Decision:** the registry moves to `markers.hpp`. `MarkerArity`, `MarkerInfo`, and `kMarkers` sit beside the
struct, with `find_marker` declared there; the setters and `find_marker` are defined in a new `markers.cpp`. The
table stays an inline `constexpr` array in the header so that tests can walk it. `docblock.cpp` looks spellings up
through it and no longer owns any of it. **Decided by:** Steve Downey.

**Log:** 2026-10-24: the new `markers.test.cpp` collided with the front end's `markers.test.cpp` target, because
`frontend/` was the one test directory whose targets did not carry the directory name. They now do
(`beman.specgen.tests.frontend.<component>`). `docblock.test.cpp` keeps its registry walk, since that case checks
that `parse_docblock` honors each row.

### [umbrella-header-fate](#umbrella-header-fate)

**Question:** what is `specgen.hpp` for? **Status:** DONE, 2026-10-24. It aggregates every header except the front
end, so it is neither everything nor a coherent subset. No production TU includes it, and it is the only route by which
`foundation/traverse.hpp` is reached outside its test. Either it is the public face of the library, in which case it
needs a definition of what that is, or it is dead and takes `traverse.hpp`'s liveness question with it.

**Decision:** it is dead, and is deleted. specgen is a tool, not a library (decision `tool-not-library`), so there
is no public face to define. Its one real job was supplying the include list for the module lane, so that list
moves into `specgen.cppm`, and the [component recipe](../decisions/component-recipe.md) adds a new header there.
**Decided by:** Steve Downey.

**Log:** 2026-10-24: `foundation/traverse.hpp` is still reached only by its test and the module unit; its liveness
is a separate question.
2026-10-25: that question went to issue #189, and `foundation/traverse.hpp` is deleted. Nothing called it.

### [untested-headers](#untested-headers)

**Question:** which headers get a test file? **Status:** DECIDED for `diagnostic.hpp`, 2026-10-23, and for
`markers.hpp`, 2026-10-24. `diagnostic.hpp` declares `severity_label`, which no test calls. `config.hpp` and
`specgen.hpp` have nothing testable beyond compiling, which the verify TUs cover. `markers.hpp` is tested only
through the registry.

**Decision:** `diagnostic.hpp` gets `diagnostic.test.cpp`, which pins each severity's label. `config.hpp` and
`specgen.hpp` get none. `markers.hpp` waits on [marker-registry-home](#marker-registry-home): if the registry moves
into it, the registry's tests are its tests. **Decided by:** Steve Downey.

**Log:** 2026-10-24: the registry moved, and `markers.test.cpp` tests it: lookup, unique spellings, each row's
arity against its setter and error message, and the setters. `specgen.hpp` was deleted
([umbrella-header-fate](#umbrella-header-fate)).

### [recursion-schemes-test-home](#recursion-schemes-test-home)

**Question:** where does `foundation/recursion_schemes.test.cpp` belong? **Status:** DECIDED, 2026-10-23. It tests
`vendor/tree_algorithms`'s `recursion_schemes.hpp`, a subtree header, under a foundation name that has no
foundation header. It is an acceptance test for the vendored code; it should say so in its name or location.

**Decision:** it moves to `tests/beman/specgen/vendor/recursion_schemes.test.cpp`. The location says it is a test of
vendored code and the name still matches the header; it stays on specgen's side because it tests the seam specgen
relies on, and the subtree has its own `recursion_schemes.test.cpp`. **Decided by:** Steve Downey.

### [document-build-reinclusion](#document-build-reinclusion)

**Question:** none; `document_build.test.cpp` is the one tested header missing the re-inclusion line.
**Status:** DONE, 2026-10-23.

**Log:** 2026-10-23: `foundation/enumeration.test.cpp`, added since the review, was missing it too; both now have
it. `frontend/frontend.test.cpp` checks re-inclusion inside a test case instead, which counts.

### [tests-of-dependencies](#tests-of-dependencies)

**Question:** which assertions belong to another header's test file? **Status:** OPEN. The cases found:

- `backend/mpark.test.cpp` and `backend/org.test.cpp` assert `common::element_label` directly; `org.test.cpp`
  pins `common::draft_span_codeblock`.
- `conjuncts.test.cpp` verifies layout by rendering through `backend::latex::render_to_string`, so a LaTeX change
  fails a conjuncts test, and one case there tests the IR JSON round trip.
- `validate/validate.test.cpp` re-tests monoid laws already covered by `foundation/monoid.test.cpp`.
- `ir.test.cpp` tests `json_writer`'s escaping through `emit_json`.
- `frontend/document.test.cpp` asserts `validate::validate` on a built document; `frontend/diagnostics.test.cpp`
  duplicates a `parse_rsec` case that belongs in `rsec.test.cpp`.

Some of these are integration checks worth keeping under that name; the rule is that they do not masquerade as the
dependent facility's unit tests.

**Status:** DECIDED, 2026-10-23. **Decided by:** Steve Downey.

**Decision:** case by case.

- The `element_label` assertions in `mpark.test.cpp` move to `backend/common.test.cpp`, which also gains direct
  cases for `draft_span_prose`, `draft_span_codeblock`, and `draft_code_inline`. The per-backend "every element
  kind renders as its draft label" cases and `org.test.cpp`'s use of `draft_span_codeblock` stay: they check that
  the backend uses the shared helper, which is the backend's claim.
- `conjuncts.test.cpp` asserts on the IR `render_into` builds, not on LaTeX. The layouts it was pinning through the
  LaTeX backend, an itemize with and without lead-in prose, are now `latex.test.cpp` cases, and the itemize JSON
  round trip is an `ir.test.cpp` case.
- `validate.test.cpp`'s monoid-law cases stay. They test `diagnostics_monoid`, an instance `validate.hpp` itself
  defines, not the generic laws `monoid.test.cpp` covers.
- `ir.test.cpp`'s string-escaping case goes: `json_writer.test.cpp` pins each escape, and the IR's claim, that a
  string needing escapes survives the round trip, is the existing round-trip case.
- `document.test.cpp`'s `validate::validate` checks stay, as the end-to-end claim that a built document validates
  clean.
- `diagnostics.test.cpp`'s `parse_rsec` failure-offset case moves to `rsec.test.cpp`.

### [cpp-transitive-includes](#cpp-transitive-includes)

**Question:** none; the 183 `misc-include-cleaner` findings in production `.cpp` files. **Status:** DONE, 2026-10-24.
The fix is `clang-tidy --fix` under review, and it is the precondition for the check joining the lint pass.

**Log:** 2026-10-24: 282 findings by then, the growth mostly `foundation::is` and `foundation::table` reached
without including `enumeration.hpp`; `tools/specgen/main.cpp` had none left. `--fix` added the 58 missing includes
and dropped two, which were then moved into each file's existing blocks (it spells project headers in quotes and
puts Clang headers outside the `-Wnonnull` pragma in `frontend.cpp`). The check now runs as `style.include-cleaner`,
the same pass and scope as `style.no-raw-loops`.
