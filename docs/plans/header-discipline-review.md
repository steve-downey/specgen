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

**Question:** which facilities in `frontend/frontend.hpp` belong in headers of their own? **Status:** OPEN.
The header bundles at least five: `smoke_check` (a link probe), `collect_interleaved` and `SourceItem`,
`ParseOptions` with `filter_compile_command_args` and `resolve_extra_args` (pure string handling, no Clang), the
`parse_rsec` combinator parser, and `build_document`. The header's own comments call two of them out as
Clang-free and exposed for testability. The 18 test files are the visible symptom: `rsec.test.cpp`,
`collect.test.cpp`, and `frontend.test.cpp` each test a distinct facility, while the other 15 slice `build_document`
by corpus feature. That last group is scenario slicing of one facility, which the rule allows; the first three are
facilities without a header. Splitting touches the tier boundary described by decision `ir-boundary`, so it is a
design change, not a move.

### [backend-common-facilities](#backend-common-facilities)

**Question:** what are the facilities in `backend/common.hpp`, and which backends consume each? **Status:** OPEN.
Four live there with different consumer sets: the element-label table (mpark, org), `render_code_spans` (all),
the draft-LaTeX span spellings (latex, org), and the `RenderF` base functor. `common.test.cpp` tests only
`render_code_spans`; the label table is asserted from `mpark.test.cpp` and `org.test.cpp` instead, and the span
spellings from `org.test.cpp`. The name says "shared", which is a layer, not a facility.

### [ir-serialization-home](#ir-serialization-home)

**Question:** does JSON emit and parse belong in `ir.hpp`? **Status:** OPEN. The header carries the node types,
`canonicalize`, the name tables, and the JSON contract with three deprecated shims. Decision `node-base-functor`
already chose to keep `ir.hpp` byte-identical and put the functor beside it in `ir_fold.hpp`, which is the pressure
this bundling creates. `ir.test.cpp` and `ir_fold.test.cpp` both pin the JSON schema, the second one by
re-implementing emission through `json_writer`, and the file's own comment acknowledges the duplication.

### [json-reader-home](#json-reader-home)

**Question:** is the `Reader` lexer in `foundation/json_descriptor.hpp` a facility of its own? **Status:** OPEN.
It is about 240 lines of hand-written JSON lexing inside a header named for descriptors; the head comment
rationalizes the co-location. `json_writer.hpp` is the precedent for the other direction.

### [marker-registry-home](#marker-registry-home)

**Question:** should the marker registry sit with the marker struct? **Status:** OPEN. `markers.hpp` holds the
struct and claims a single definition; the table, arity enum, and setters live in `docblock.hpp`, and the only test
of the struct is inside `docblock.test.cpp`. Decision `marker-registry` names one table, not one header.

### [umbrella-header-fate](#umbrella-header-fate)

**Question:** what is `specgen.hpp` for? **Status:** OPEN. It aggregates every header except the front end, so it is
neither everything nor a coherent subset. No production TU includes it, and it is the only route by which
`foundation/traverse.hpp` is reached outside its test. Either it is the public face of the library, in which case it
needs a definition of what that is, or it is dead and takes `traverse.hpp`'s liveness question with it.

### [untested-headers](#untested-headers)

**Question:** which headers get a test file? **Status:** OPEN. `diagnostic.hpp` declares `severity_label`, which no
test calls. `config.hpp` and `specgen.hpp` have nothing testable beyond compiling, which the verify TUs cover.
`markers.hpp` is tested only through the registry.

### [recursion-schemes-test-home](#recursion-schemes-test-home)

**Question:** where does `foundation/recursion_schemes.test.cpp` belong? **Status:** OPEN. It tests
`vendor/tree_algorithms`'s `recursion_schemes.hpp`, a subtree header, under a foundation name that has no
foundation header. It is an acceptance test for the vendored code; it should say so in its name or location.

### [document-build-reinclusion](#document-build-reinclusion)

**Question:** none; `document_build.test.cpp` is the one tested header missing the re-inclusion line.
**Status:** OPEN, one-line fix.

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

### [cpp-transitive-includes](#cpp-transitive-includes)

**Question:** none; the 183 `misc-include-cleaner` findings in production `.cpp` files. **Status:** OPEN, mechanical.
The fix is `clang-tidy --fix` under review, and it is the precondition for the check joining the lint pass.
