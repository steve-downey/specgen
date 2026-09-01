<!-- SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception -->
<!-- markdownlint-disable MD013 -->

# foundation/ — divergence log

This file is the usage-experience feedback channel back to the sibling repos
(`tree_algorithms`, `compile-time-scheme`, `transpose`), per the
**subtree-consumption** decision. Every friction point, adaptation, and wish
found while adopting their idioms is recorded here as it happens.

Entries that warrant upstream action are filed as issues (the current batch
went up on 2026-08-15), and the issue bodies are kept alongside in
[`UPSTREAM-ISSUES.md`](UPSTREAM-ISSUES.md). This file stays the place new
observations land; that one records what was sent and where.

Ground rules (decision subtree-consumption):

- The `vendor/tree_algorithms` subtree is consumed **read-only**. Local edits to
  vendored files are forbidden — a needed change becomes an entry here and an
  upstream issue, never an in-tree patch, so subtree pulls stay clean.
- Pieces that need runtime adaptation are **ported** into this directory (not
  subtree'd) with a provenance header naming the source repo, commit, and path.
  Divergence from the source is expected for these by construction; the notes
  below say how and why.

## Decisions

### DEC-1 — Fallible carrier: `std::expected` (decision expected-error-taxonomy)

**Decision.** specgen's fallible carrier is **`std::expected<T, E>`** (C++23,
shipping in libstdc++ 16 and usable under Clang 22/23 with libstdc++ — the
cxx26-baseline toolchain matrix). The vendored/ported helpers
(`fold_left_short`, `traverse`) are written against it once, here.

**Spike.** The expected-error-taxonomy decision asked whether
`steve-downey/expected` — the implementation with
`T&` / `E&` reference support ("Expected over References", D4280R0) — pulls its
weight over `std::expected`. Findings:

- The claimed win is that reference support lets `and_then` chains pass large IR
  pieces without copies. But specgen's fallible surfaces are value-producing
  (parse results, derived elements, `expected<ir::Document, …>`); `and_then`
  already threads those by **move**, and moving an `ir::Document` (a `vector` of
  nodes) is a pointer-swap, not a deep copy. Reference-`T` would save the move,
  which is not a cost that shows up here.
- `std::expected` is standard, zero-maintenance, and already models the
  `short_circuit_effect` concept `fold_left_short` needs, so the ported helper
  needed no carrier-specific rewrite.
- `beman.expected` is self-described "under development, not yet ready for
  production", and adopting it means a second subtree carrying churn into
  specgen CI, for a benefit the call sites do not exercise.

The decision named `std::expected` as the fallback "if the spike shows it does
not pull its weight." It does not, for specgen's shapes. **Revisit** if a
future need (e.g. validators threading references to shared AST/IR through
long `and_then` pipelines) develops a real copy cost that reference-`T`/`E`
would remove.

## Observations against the sibling repos

### OBS-1 — `tree_algorithms` ships the `overloaded` tripwire

An expected gap that turned out not to exist: `tree_algorithms`'
`overloaded.hpp` was believed to **lack** the consteval
`static_assert(false)` exhaustiveness tripwire (present in
`compile-time-scheme`'s `src/smd/fixpoint/overloaded.hpp`), and closing that
gap was expected to be the first divergence-log entry and upstream suggestion.

As vendored (commit `0587577`), `vendor/tree_algorithms/include/beman/tree_algorithms/overloaded.hpp`
**carries the tripwire**, identical to the compile-time-scheme version. The
gap was closed upstream before specgen vendored the code — no issue to file.
(OBS-5 below is a *different* defect in the same header, and was filed.)

We still keep a project-local `foundation/overloaded.hpp` (ported from
compile-time-scheme) rather than consuming `beman::tree_algorithms::overloaded`
from the subtree, so the visitor builder lives in the `beman::specgen::foundation`
namespace and does not tie visitation sites to a vendored include path. This is a
deliberate, low-cost duplication, not a divergence forced by a defect.

### OBS-2 — Explicit-parameter tier sufficed (decision no-typeclass-objects)

specgen takes only the **primary (explicit-parameter)** tier of the
vendored verbs: `fold_with` receives its `fmap`/`project` as explicit arguments,
and monoids are the explicit `{combine, identity}` pair (`foundation/monoid.hpp`),
never the CRTP `Functor` base / `functor_typeclass` lookup that
`tree_algorithms` and `transpose` label temporary P3200 stubs. "The explicit
tier sufficed for a real tool" is itself the feedback: the lookup tier was not
needed. **Filed** as a usage report:
[transpose#34](https://github.com/steve-downey/transpose/issues/34).

### OBS-3 — `keyword` and `digits` are new, not upstream ports

The parser-combinators decision names `keyword` and a checked `digits` in the
runtime-ized primitive set,
but neither exists under that shape in `compile-time-scheme`'s
`src/smd/smdscheme/parser/` at the pinned commit (`4b0b216`): the closest
things are `keyword_p()` in `smdlisp/reader/atom.hpp` (a Lisp `:foo`
keyword-datum reader, an unrelated concept) and the unchecked
`some<20>(...)` + manual digit-to-int loop backing `smdscheme`'s integer atom
parser (still no `std::from_chars`, still no overflow check). Both were
written fresh in
`foundation/parse/parser.hpp`, in the same char-at-a-time /
commit-on-consumed-input style as the ported `char_p`/`satisfy`, not invented
independently of that style. Worth upstreaming: a whole-spelling
`keyword(string_view)` combinator and a `std::from_chars`-backed checked
integer parser would benefit `smdscheme` too, whose own `some<20>` plus manual
accumulation has the identical unchecked-overflow shape specgen's `digits()`
exists to remove. **Filed**, together with OBS-4's documentation note, as
[compile-time-scheme#47](https://github.com/steve-downey/compile-time-scheme/issues/47).

### OBS-4 — Sibling markers sharing a `\` prefix are not `keyword | keyword`

Chaining `keyword(a) | keyword(b)` only falls through to `b` when `a` fails
*before* consuming any input. specgen's docblock markers (`\pre`, `\post`,
`\at`, ...) and section headers (`\rSec`, `\ref`) all share a leading `\`, so
naively alternating `keyword("\\pre") | keyword("\\post") | ...` would commit
on the shared `\` and never reach a sibling once the first candidate's second
character mismatched — confirmed by a negative test in
`tests/beman/specgen/foundation/parse/parser.test.cpp`
("operator| - a committed (consumed) failure is final ..."). This is not a gap
in the primitive set: `docblock.cpp`'s `find_marker` sidesteps
it by scanning the identifier once (`[a-zA-Z-]+`) and dispatching the resulting
name through a table, which is the shape to keep — `many`/`satisfy` to
scan the name, then a table lookup, never a `keyword` alternation over
siblings. `keyword` is for a single spelling already known to be the only
candidate at that grammar position (e.g. `\rSec` once a comment is known to be
a structural marker, not a prose line).

### OBS-5 — The `overloaded` tripwire needs a *deduced* return type

Both `compile-time-scheme` (`f60b0ff`) and the vendored `tree_algorithms`
(`0587577`) spell the exhaustiveness catch-all with a hard-coded `void` return:

```c++
consteval void operator()(auto) const {
    static_assert(false, "overloaded: unhandled variant alternative — add a case");
}
```

That works only for a **void-returning** visitor. `std::visit` requires every
alternative's call to yield the same return type, and that check needs each
call's return *type* alone — so against a value-returning visitor the `void`
catch-all mismatches before its body is ever instantiated, the `static_assert`
never fires, and what the author sees instead is libstdc++'s
`variant:1976: static assertion failed: std::visit requires the visitor to have
the same return type for all alternatives of a variant`. The build still fails
(the tripwire is fail-safe), but the message does not name the omission,
which is the entire reason foundation carries its own `overloaded`.

This is not hypothetical and not specific to the validator: deleting the
`ir::FreeParagraph` case from `latex.cpp`'s `SeededProjector` (which returns
`common::RenderF<Seeded>`) reproduces the generic message. Most visitors in
this tree return values — `ir_fold.hpp`'s three, `latex.cpp`'s renderers,
`validate.cpp`'s `ValidationAlgebra` — so a `void`-only tripwire would hold
only for the likes of `ir.cpp`'s void-returning `NodeEmitter`.

**Divergence:** specgen's port declares the catch-all `consteval auto`.
Deducing the return type forces the body to be instantiated to compute it,
which puts the named `static_assert` first. Verified by deleting a case with
each spelling and capturing the compiler output both ways.

**Worth upstreaming to both siblings** — a one-word change that restores the
intended diagnostic wherever a visitor returns a value. **Filed:**
[tree_algorithms#2](https://github.com/steve-downey/tree_algorithms/issues/2)
and
[compile-time-scheme#46](https://github.com/steve-downey/compile-time-scheme/issues/46).
