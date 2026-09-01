<!-- markdownlint-disable MD013 -->

# Coding Rules

Read this before writing code; it binds human and AI authors.

This file says nothing about repository layout, CMake, or the build —
`docs/building.md` governs those, and repeating them here would just give
them a second place to drift out of sync. Where this file
cites a design decision, the record lives under `docs/decisions/`; those
documents win on rationale, this one wins on "is this diff acceptable."

## File prolog and includes

- Every source-like file opens with a repo-relative path comment and an
  Emacs mode line, then SPDX on the next comment-capable line. Any file
  under `include/beman/specgen/foundation/` shows the form.
- Header guards are classical, named from the repo-relative path (for
  example `BEMAN_SPECGEN_FOUNDATION_OVERLOADED_HPP`). Never `#pragma once`.
- Include project headers only by canonical angle-bracket spelling
  (`#include <beman/specgen/foundation/overloaded.hpp>`) — never relative,
  never by leaf name, never relying on a transitive include. Headers are
  self-contained and never contain `using namespace`.
- Declare functions before defining them; define ordinary member and free
  functions out of line, qualified with full namespace and class scope. The
  one exception is a hidden friend for a customization point, kept short and
  clearly marked.

## Baseline and tooling

- C++26 on GCC 16, clang 22 and 23, all with libstdc++
  ([cxx26-baseline](decisions/cxx26-baseline.md)). No fallback paths for older
  standards or other compilers.
- If an API can be meaningfully `constexpr`, make it `constexpr` and pin the
  contract with a compile-time test alongside the runtime ones —
  `foundation/fold_left_short.hpp`, `foundation/traverse.hpp`, and
  `foundation/monoid.hpp` all do this today.
- **Interpolate with `std::format`; print with `std::print`/`std::println` to
  a `FILE*`** ([format-print-output](decisions/format-print-output.md)).
  Never build a message by `+` and `std::to_string`, and
  never thread a `std::ostream&` through a producer so that its leaves can
  `<<` into it — return the text and let the one caller that has a sink write
  it. Streams keep one job: moving bytes that are already built
  (slurping a file or stdin, writing a finished document to an `ofstream`),
  which is why `read_all` in `tools/specgen/main.cpp` still uses one. A
  format string is a literal; text that merely *is* a string literal, like the
  driver's usage block, is passed as an argument to `"{}"` instead, so a brace
  someone adds to it later is a character and not a parse error.
- Formatter and lint configuration (`.clang-format`, `.markdownlint.yaml`,
  `pre-commit`) is a binding contract.

## Typeclasses: not adopted

specgen takes only the **explicit-parameter tier** of the fold/traverse/monoid
vocabulary `tree_algorithms` and `transpose` each offer in two tiers. Every
vendored or ported verb (`fold_with`, `traverse`, `mconcat`) receives its
operations (`fmap`, `project`, `combine`/`identity`) as explicit arguments,
the way `foundation/monoid.hpp`'s `mconcat(range, monoid{combine, identity})`
already reads.

Do not build a typeclass-object framework on top of this: no CRTP typeclass
bases, no `*_typeclass` variable-template lookup, no `Foldable` /
`Applicative` / `Traversable` specialization hung off `ir::Node`. specgen has
on the order of four functor-shaped types (`NodeF`, `vector`,
`optional`/`expected`, the diagnostics carrier) and on the order of ten call
sites over them — explicit parameters cost less than a lookup framework sized
for a much larger zoo, and both source repos themselves describe that lookup
tier as a temporary stub. Declining it here is itself feedback to the sibling
repos ([no-typeclass-objects](decisions/no-typeclass-objects.md)). Revisit
only once that stub stabilizes upstream; moving an
explicit call site to a lookup later is mechanical, so there is no cost to
waiting.

## Visitation

The decision record is [visitation-rules](decisions/visitation-rules.md).

- `beman::specgen::foundation::overloaded` (the tripwire variant in
  `include/beman/specgen/foundation/overloaded.hpp`) is the visitor builder
  for every `std::visit` over an IR variant. Its `consteval` catch-all turns
  a forgotten alternative into a compile error naming the case, never a
  silent fall-through or default.
- `std::visit` may dispatch **one node's own alternatives** inside an
  algebra. It must not drive recursion — recursion belongs to `fold_with`
  ([node-base-functor](decisions/node-base-functor.md),
  [backend-direct-algebra](decisions/backend-direct-algebra.md)). The two
  sanctioned shapes are live in the tree: `backend/latex.cpp`'s `render_node`
  is a `fold_with` algebra over `RenderF`, and `ir.cpp`'s `emit_json(Node)`
  does not recurse at all — descriptor tables own the walk. So if a visitor
  of yours wants to call itself, either lift the recursion into a fold or
  move the traversal into a table, and do not reintroduce the shape.
- A fold that resolves an **inherited** attribute on the way down (a depth, a
  section context) uses `backend/common.hpp`'s `RenderF`, seeded with the
  attribute; one that does not uses `ir::NodeF` / `node_project` and does its
  combining on the way back up, as `validate.cpp` does. The rule is stated at
  `RenderF`'s definition; parking a resolved value in a field named for
  something else is exactly the failure it guards against.
- Overloaded-lambda sets for ≤3 stateless cases. Above that, or the moment a
  case needs state, write a named visitor struct instead: a doc comment per
  case (so a case has a name in a diagnostic, not a lambda's line number),
  member state instead of captures. Every backend visitor and the JSON
  descriptor walker are named structs by this rule.

## Error taxonomy

The decision record is
[expected-error-taxonomy](decisions/expected-error-taxonomy.md).

Four shapes, four tools. Do not blend them at one call site.

| Shape | Tool | Outlawed |
| --- | --- | --- |
| A single fallible step | `std::expected<T, E>` + `.and_then(...)` | `if (!r.has_value()) return r;` ladders |
| A sequence that must stop on first failure | `fold_left_short` (`foundation/fold_left_short.hpp`) | a hand-rolled loop with a `break` on error |
| A structure to thread the effect through, preserving shape | the concrete overloads in `foundation/traverse.hpp` (`sequence`, `traverse`), or a `fold_with` over `ir_fold.hpp`'s `NodeF` for the IR tree | a hand-rolled accumulate-and-check loop |
| Multiple diagnostics that must all be reported, not just the first | Validation style — carry `{value, vector<Diagnostic>}` and combine with the diagnostics monoid (`combine` = concatenation, `identity` = empty) | fail-fast `and_then` |

This is an assignment; specgen splits
along it: IR JSON parsing (`ir.cpp`'s `Reader`) and `parse_rsec` are
fail-fast — in a serialization format the first error is definitive, so both
carry `expected` plus a position, not Validation.
`grammar::ParseResult` (`include/beman/specgen/docblock.hpp`) is the
accumulating shape — a `Docblock` plus `vector<Diagnostic>` across three
severities — and `validate/validate.hpp`'s `Diagnostics` monoid, folded over
`NodeF`, is the same shape for the IR tree; the validators are further
algebra cases on it, not new traversals. Driver and frontend orchestration is
an `and_then` pipeline over stages: no accumulation belongs at that layer,
because a broken earlier stage makes every later stage's output meaningless.

One boundary recurs across the tree: a combinator or `expected`
failure **inside** a stage is not automatically the diagnostic a user sees.
`docblock.cpp` parses with combinators and still accumulates its three
severities, converting parse failures to diagnostics at the boundary;
`parse_rsec` reports a malformed marker only once the tag itself is recognized,
because a failure *before* the tag means "not a `\rSec` comment at all".
Choose the row by what the caller must do with the outcome, not by which carrier
the code happens to be holding.

`foundation::monoid<T, Combine>` is the explicit `{combine, identity}` pair
([no-typeclass-objects](decisions/no-typeclass-objects.md)) both
`mconcat`/`mconcat_map` and the Validation diagnostics carrier are
built from. It is a value you construct and pass, not a typeclass instance
looked up by type.

## No raw loops

A `for`/`while` loop outside a substrate generic algorithm is a defect.
Before writing one, name which algorithm it actually is:

- A `for_each` mutating a captured accumulator is a fold
  (`fold_left`/`fold_left_short`/`mconcat`).
- A write **at the current position** is an append: build the result with
  `transform` into a fresh container, not by indexing into one pre-sized to
  match the input.
- **Two sequences walked together is `views::zip`, not `iota` plus two
  subscripts.** What decides whether an index survives is where the pass
  *writes*, not whether it happens to mention one: a read at the current
  position is a zip element, a read at another position is ordinary random
  access and is fine, and only a **write** at another position (a scatter)
  actually needs the index.
- **A loop whose whole job is a side effect stays a loop.** If the body
  triggers an effect per element (appending through a callback, recursing
  into a collector, assigning to the element it is iterating) and builds no
  result, `std::ranges::for_each` would replace the `for` keyword with a call
  and change nothing else. This tree declines that trade: it hides the loop
  instead of removing it, and the gate would then be satisfied by a costume.
  Keep the loop and mark it. Filtering such a walk is still an improvement and
  is encouraged — `views::filter(pred)` states up front what an `if` at the
  top of the body buries — so the marked form is usually
  `for (x : range | views::filter(pred))`.
- Only a genuine scatter, or a loop that *implements* one of the primitives
  above rather than calling it, stays a raw loop — and it then carries the
  exact comment `// substrate generic algorithm` plus a one-line reason. This
  is not a rule waiting to be adopted: `foundation/fold_left_short.hpp`,
  `foundation/traverse.hpp`, and `foundation/monoid.hpp` each mark their one
  internal loop this way today. The marker goes **on the loop line itself, or
  in the contiguous comment block immediately above it**; a comment above the
  enclosing function credits none of the loops inside it, because a reader
  standing at the loop should be able to see why it is allowed to be there.

`tools/check-raw-loops.cmake` is the grep-able gate this doctrine runs as:

```sh
cmake -P tools/check-raw-loops.cmake                                  # whole tree
cmake -DPATHS="<file-or-dir>[;<file-or-dir>...]" -P tools/check-raw-loops.cmake  # one file or dir
```

It is wired into ctest as **`style.no-raw-loops`** (`ctest -R style` selects
it), registered unconditionally so it runs and reports the same result in both
build configurations — it reads source text, not compiled output. Its scope is
`src/`, `include/`, and `tools/`'s `*.cpp`/`*.hpp`/`*.cppm`, with two
exclusions: `vendor/` (a subtree;
[subtree-consumption](decisions/subtree-consumption.md) forbids local edits)
and `tests/` plus any
`*.test.cpp` (out of this doctrine's inventory, which covers production code
only). Every loop inside that scope must carry the marker, or the ctest case
fails, naming the file and line.

The live check above is what enforces the doctrine.

## PR closing checklist

Run through this, in order, before calling an increment or PR done:

- [ ] The build and test suite are green. `docs/building.md` has the
      commands and the current expected test count, which moves whenever a
      case lands, so don't hard-code it here.
- [ ] `make goldens` produces an empty
      `git diff --stat tests/golden/`. A non-empty diff is a **finding to
      report**, never an edit to the golden.
- [ ] `make lint` is clean **on its second run** — the first reformats and
      fails by design; `git add -A` the reformatting and run it again, and
      that second result is the one that counts. This covers **markdown** as
      well as C++ and CMake: the markdownlint hook is enabled, excluding the
      `vendor/` and `infra/` subtrees. `markdownlint-cli2 --fix` handles the
      bulk of what it reports, but read the diff before taking it — it can
      silently rewrite a line-wrapped `+` (joining two phrases in a sentence)
      into a `-` list bullet, which changes the prose's meaning.
- [ ] Every loop you touched or added is an algorithm call, or carries
      `// substrate generic algorithm` plus its one-line reason. The marker's
      *presence* is machine-checked (`style.no-raw-loops`); this item is about
      the *reason* being honest, not the marker existing.
- [ ] Every `std::visit` you added goes through `overloaded` and dispatches
      only its own node's alternatives — no recursion inside the visitor.
- [ ] Every fallible step you added is assigned to exactly one row of the
      error taxonomy above, not a blend of two.
- [ ] No golden file was hand-edited to make a diff disappear.
