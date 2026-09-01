# The acid test

The acid test runs specgen against the real `beman.optional` header, out of tree,
and compares the generated wording with the corresponding frozen draft LaTeX. The
in-tree corpus is hermetic by design (see
[decisions/hermetic-corpus.md](decisions/hermetic-corpus.md)); this procedure is the
measurement against the real 2,145-line header. The annotated headers and generated
files live under the gitignored `scratchpad/acid/`; this document records enough
provenance and commands to repeat or audit a run.

## Frozen inputs

### Upstream header

- Repository: `/home/sdowney/src/steve-downey/optional/main`
- Revision: `ff46759b0028ea80b13d0277945bfa4fd0076327`
- File: `include/beman/optional/optional.hpp`
- File SHA256: `c12f085d7f9c8d6dfaaf99c6dc2e12c85910852796524c2823fd57d1cf02006b`
- Size: 2,145 lines
- Native specgen markup: none; the upstream file contains no `//!` docblocks

The last commit touching the file at that revision is
`8c6390e8149c5a5f50637eb075dcbba2c2d7e560` (2026-04-13,
"Implement proposed fix for LWG4497"). Confirm the input before annotating:

```sh
UPSTREAM=/home/sdowney/src/steve-downey/optional/main
git -C "$UPSTREAM" rev-parse HEAD
wc -l "$UPSTREAM/include/beman/optional/optional.hpp"
sha256sum "$UPSTREAM/include/beman/optional/optional.hpp"
rg -n '^//!' "$UPSTREAM/include/beman/optional/optional.hpp"
```

The cumulative annotated stage-4 input used by the final run is
`scratchpad/acid/optional.stage4.hpp`, SHA256
`9127daeec6b6887032b8d94cfa22af2af3e23ad495ddb3a18f3ded497a6490e5`.
Annotations are experimental inputs, not changes proposed for upstream.

### Frozen draft

- Repository: `/home/sdowney/src/steve-downey/draft/draft`
- Revision: `831d3cb7d6cebd9e5d0202c5b3af7b482762146d`
- Source: `source/utilities.tex`
- Complete optional comparison range: lines 3495-5963
- Range SHA256: `7ce125425de00e96c82819398302eecf56a3b5b0da5402102bdca7e9ee5e4d99`

The owned draft slices are exact source ranges, not a moving line selection from
generated output:

| Stage | Draft source lines | First through last owned material | Lines |
| --- | ---: | --- | ---: |
| 1 | 3603-4332 | `[optional.optional.general]` through `[optional.assign]` | 730 |
| 2 | 4333-4785 | `[optional.swap]` through the first `[optional.mod]` | 453 |
| 3 | 4786-5409 | `[optional.optional.ref]` through `[optional.bad.access]` | 624 |
| 4 | 5410-5963 | `[optional.relops]` through `[optional.hash]` | 554 |

Recreate them with, for example:

```sh
DRAFT=/home/sdowney/src/steve-downey/draft/draft
git -C "$DRAFT" show 831d3cb7:source/utilities.tex | \
  sed -n '4333,4785p' > scratchpad/acid/draft.stage2.slice.tex
```

## Toolchain and binary

A run uses the Clang-enabled `gcc-release` preset build: GCC 16 with the LLVM/Clang
22 front end (see `docs/building.md`). Record the specgen revision — and, if a
binary is frozen for later comparison, its SHA256 — alongside the run's outputs so
the run stays auditable. The locally installed GCC runtime must be visible both
while CMake discovers Catch2 tests and while specgen runs:

```sh
export LD_LIBRARY_PATH=/home/sdowney/install/gcc-16/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}

uv run cmake --preset gcc-release \
  -DClang_DIR=/usr/lib/llvm-22/lib/cmake/clang
uv run cmake --build --preset gcc-release
uv run ctest --preset gcc-release
```

`LD_LIBRARY_PATH` fixes the process runtime only. Embedded Clang still needs the
GCC installation and project include path. Every generation uses this exact parser
tail:

```text
-- -std=c++2c --gcc-toolchain=/home/sdowney/install/gcc-16 \
   -I /home/sdowney/src/steve-downey/optional/main/include
```

Do not omit `--no-compile-commands`; the acid run must not depend on a nearby
developer build database.

## Staging and annotations

Each stage is cumulative in source coverage, while its owned comparison is a
disjoint stable-name slice. This prevents later unannotated declarations from
turning an early measurement into apparent regressions.

| Stage | Owned subclauses | Draft items | Generated items | Signatures |
| --- | --- | ---: | ---: | ---: |
| 1 | general, constructors, destructor, assignment | 17 | 17 | 18 |
| 2 | swap, iterators, observers, monadic operations, first modifier | 20 | 20 | 30 |
| 3 | reference specialization, nullopt, bad access | 25 | 25 | 25 |
| 4 | relational/nullopt/T comparisons, algorithms, hash | 27 | 27 | 27 |

Stage 1's extra signature is an overload grouped into one draft item. Stage 2 has
two iterator-alias signatures in one item and four named monadic groups; that is
why 20 items contain 30 signatures. Stage 3's nullopt item stores two declarations
in one exact `CodeText`, so its 25 signatures represent 26 authored declarations.

The cumulative annotation inventory is:

1. Stage 1: section markers and wording for the primary template's general,
   constructor, destructor, and assignment clauses; exposed helper concepts and
   state; conditional `explicit`/`noexcept` masks; authored lists, references, and
   four assignment tables.
2. Stage 2: swap wording/table, routed iterator aliases with
   implementation-defined RHSs, hardened observer preconditions, named non-adjacent
   monadic groups, and all definitions owned by the first modifier clause.
3. Stage 3: the reference specialization, its in-class templates and exposed
   helpers, exact nullopt declarations, bad-access wording, and recovered physical
   section markers.
4. Stage 4: 26 documented namespace free-function definitions, authored relational
   constraints expanding implementation helper concepts, the specialized
   algorithms, and one exact hash item. The real hash record is `\merge`d so it
   supplies no second synopsis or coverage roster.

The source contains a later second `optional.mod` and leaves `optional.ref.expos`
open over implementation material. Owned slicing handles both facts structurally; a
prefix grep is not a valid stage boundary.

## Reproduction

Generate and render one cumulative stage as follows (replace `N`):

```sh
SPECGEN=build/gcc-release/tools/specgen/specgen
OPTIONAL=/home/sdowney/src/steve-downey/optional/main

"$SPECGEN" generate --emit-ir \
  "scratchpad/acid/optional.stageN.hpp" --no-compile-commands -- \
  -std=c++2c --gcc-toolchain=/home/sdowney/install/gcc-16 \
  -I "$OPTIONAL/include" \
  > "scratchpad/acid/generated.stageN.json" \
  2> "scratchpad/acid/generate.stageN.stderr"

"$SPECGEN" render --from-ir "scratchpad/acid/generated.stageN.json" \
  > "scratchpad/acid/generated.stageN.tex" \
  2> "scratchpad/acid/render.stageN.stderr"
```

Generate exits 0 for every final stage. Ordinary render exits 0 with empty stderr.
The earlier cumulative sources emit warnings for numbered draft-style headings not
yet converted to `\rSec` markers: six for stages 1-2, four for stage 3, and none
for stage 4.

Build owned IR files by selecting document nodes; rendered line numbers move:

```sh
# Stage 1
jq '{nodes: [.nodes[] | select(.type=="section" and
      (.stable=="optional.optional.general" or .stable=="optional.ctor" or
       .stable=="optional.dtor" or .stable=="optional.assign"))],
     foreign, body_uses}' \
  scratchpad/acid/generated.stage1.json \
  > scratchpad/acid/generated.stage1.owned.json

# Stage 2: take the first five matching sections, then only the first
# optional.mod item because that stable name is reused later in the source.
jq '{nodes: ([.nodes[] | select(.type=="section" and
      (.stable=="optional.swap" or .stable=="optional.iterators" or
       .stable=="optional.observe" or .stable=="optional.monadic" or
       .stable=="optional.mod"))] | .[0:5] |
      map(if .stable=="optional.mod" then
        .children = [.children[] | select(.type=="item")][0:1]
      else . end)), foreign, body_uses}' \
  scratchpad/acid/generated.stage2.json \
  > scratchpad/acid/generated.stage2.owned.json

# Stage 3: the final exposition section stays open over later source, so keep
# only its stage-owned items.
jq '{nodes: [.nodes[] | select(.type=="section" and
      (.stable=="optional.nullopt" or .stable=="optional.bad.access" or
       .stable=="optional.optional.ref")) |
      if .stable=="optional.optional.ref" then
        .children |= map(if .type=="section" and
          .stable=="optional.ref.expos" then
            .children = [.children[] | select(.type=="item")]
          else . end)
      else . end], foreign, body_uses}' \
  scratchpad/acid/generated.stage3.json \
  > scratchpad/acid/generated.stage3.owned.json

# Stage 4
jq '{nodes: [.nodes[] | select(.type=="section" and
      (.stable=="optional.relops" or .stable=="optional.nullops" or
       .stable=="optional.comp.with.t" or .stable=="optional.specalg" or
       .stable=="optional.hash"))], foreign, body_uses}' \
  scratchpad/acid/generated.stage4.json \
  > scratchpad/acid/generated.stage4.owned.json
```

Validate and render each owned document:

```sh
for n in 1 2 3 4; do
  "$SPECGEN" render \
    --from-ir "scratchpad/acid/generated.stage$n.owned.json" --validate \
    > "scratchpad/acid/validated.stage$n.owned.tex" \
    2> "scratchpad/acid/validate.stage$n.owned.stderr"
  "$SPECGEN" render \
    --from-ir "scratchpad/acid/generated.stage$n.owned.json" \
    > "scratchpad/acid/generated.stage$n.slice.tex" \
    2> "scratchpad/acid/render.stage$n.slice.stderr"
done
```

Stages 2-4 exit 0 and produce empty validation stderr. Stage 1 exits 1 for
expected partial-document reasons: its whole class synopsis still contains `\ref`
groups owned by later stages, bad-access is not owned there, and it retains
intentionally recorded private-data/body-use and drift findings. Do not weaken the
validator to make that partial slice clean.

Finally, compare by stage:

```sh
diff -u scratchpad/acid/draft.stageN.slice.tex \
  scratchpad/acid/generated.stageN.slice.tex \
  > scratchpad/acid/diff.stageN.patch
```

The diff normally exits 1. It is evidence to classify, not a byte-equality gate:
the implementation and frozen draft differ in API details, wording, indexes, and
paragraph numbering.

Accepted source/draft differences are not product defects. The implementation lacks
some draft `const&&` observer overloads and `noexcept` specifiers; one upstream
`transform const&&` body names `const T&`; and hand-authored draft paragraphs need
not be textually identical to extracted source bodies.

## Current limitations

### No authored index for the hash escape hatch

The stage-4 hash item uses standalone `\verbatim-itemdecl`, which is intentionally
AST-independent and therefore has no `ItemDecl::index` source. Because the real
hash specialization is `\merge`d away before synopsis and roster derivation, there
is no declaration from which to derive the draft's
`\indexlibrarymember{hash}{optional}`. The output therefore omits that index entry.

An authored index marker would solve this, but it would add grammar, IR lowering
policy, and diagnostics for one measured case. Inferring a name from verbatim C++
would violate the escape hatch's no-parsing contract. An authored index facility is
deferred until a second real use establishes its vocabulary and placement
semantics.

### Unlabeled mixed item-description content

The draft hash wording is an unlabelled normative paragraph inside `itemdescr`. The
current `ItemDescr` contains typed description elements only; `FreeParagraph` is a
document node and cannot be interleaved with those elements. The acid annotation
conservatively records the enablement sentence as `\remarks`, which changes the
label and omits the remaining unrepresentable mixed paragraph.

Supporting this faithfully would require ordered mixed item-description content in
the IR plus changes to JSON and all three backends. No second acid case needs it,
so that schema expansion is deferred. The limitation is recorded here instead of
being papered over by moving the paragraph outside the item or by claiming
Remarks is normatively equivalent.
