<!-- SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception -->

# foundation/ — upstream issue drafts

`DIVERGENCES.md` is the running feedback channel back to the sibling repos
(decision **subtree-consumption**). This file is its discharge: the entries
that warrant upstream action, written as issue bodies, with the filed URL
recorded next to each. All four were filed on 2026-08-15; the bodies below are
what was submitted, kept here so the reasoning stays with the code that
produced it.

Not every observation becomes an issue. **OBS-1** records a gap that was already
closed upstream before specgen vendored the code, and is kept only so the
prediction it answers has an answer on record; nothing to file.

| Draft | Target | Source | Filed |
| --- | --- | --- | --- |
| 1 | `steve-downey/tree_algorithms` | OBS-5 | [#2](https://github.com/steve-downey/tree_algorithms/issues/2) |
| 2 | `steve-downey/compile-time-scheme` | OBS-5 | [#46](https://github.com/steve-downey/compile-time-scheme/issues/46) |
| 3 | `steve-downey/compile-time-scheme` | OBS-3, OBS-4 | [#47](https://github.com/steve-downey/compile-time-scheme/issues/47) |
| 4 | `steve-downey/transpose` | OBS-2 | [#34](https://github.com/steve-downey/transpose/issues/34) |

---

## Draft 1 — tree_algorithms

**Title:** `overloaded`'s exhaustiveness catch-all needs a deduced return type

**Body:**

`include/beman/tree_algorithms/overloaded.hpp` spells the exhaustiveness
tripwire's catch-all with a hard-coded `void` return:

```c++
consteval void operator()(auto) const {
    static_assert(false, "overloaded: unhandled variant alternative — add a case");
}
```

That fires as intended for a **void-returning** visitor. Against a
**value-returning** one it does not, and the diagnostic the header exists to
produce is lost.

`std::visit` requires every alternative's invocation to yield the same return
type, and it checks that using each call's return *type* alone. For a
value-returning visitor the `void` catch-all mismatches at that check — before
its body is ever instantiated — so the `static_assert` never runs. What the
author sees instead is libstdc++'s generic:

```text
variant:1976: static assertion failed: std::visit requires the visitor to have
the same return type for all alternatives of a variant
```

The build still fails, so this is not a soundness hole; it is fail-safe. But the
message does not name the omitted alternative, which is the whole reason to
prefer this `overloaded` over a plain one.

**Suggested fix** — declare the catch-all `consteval auto`. Deducing the return
type forces the body to be instantiated in order to compute it, which puts the
named `static_assert` first.

**How this was found.** Adopting these idioms in
[`specgen`](https://github.com/steve-downey/specgen), where most visitors return
values (a fold algebra, three IR-layer functors, a LaTeX renderer). The
guarantee was verified early on a void-returning visitor and assumed to
generalize; it did not. Deleting one case from a value-returning visitor
reproduces the generic message, and deleting the same case with the catch-all
spelled `auto` restores the named one — checked both ways instead of reasoned
about. Recorded as OBS-5 in specgen's `foundation/DIVERGENCES.md`.

---

## Draft 2 — compile-time-scheme

**Title:** `fixpoint/overloaded.hpp`: exhaustiveness catch-all needs a deduced return type

**Body:**

Same defect and same one-word fix as
`steve-downey/tree_algorithms` (filed there too) — `src/smd/fixpoint/overloaded.hpp`
at `f60b0ff` has the identical `consteval void` catch-all:

```c++
consteval void operator()(auto) const {
    static_assert(false, "overloaded: unhandled variant alternative — add a case");
}
```

`std::visit` compares alternatives' return *types* before instantiating any
body, so against a value-returning visitor the `void` mismatches first, the
`static_assert` never fires, and the author gets libstdc++'s generic "same
return type for all alternatives" message instead of one naming the missing
case. Fail-safe, but the named diagnostic is the point of the header.

**Suggested fix:** `consteval auto`.

Found while porting this header into
[`specgen`](https://github.com/steve-downey/specgen) (OBS-5 in its
`foundation/DIVERGENCES.md`); verified by deleting a case with each spelling and
capturing the compiler output both ways.

---

## Draft 3 — compile-time-scheme

**Title:** Parser combinators: a whole-spelling `keyword`, a checked integer parser, and a note on ordered choice

**Body:**

Three pieces of usage feedback from runtime-izing
`src/smd/smdscheme/parser/` into
[`specgen`](https://github.com/steve-downey/specgen) — two suggestions and one
documentation note. All against `4b0b216`.

**1. A whole-spelling `keyword(string_view)`.** There is no combinator that
matches a fixed multi-character spelling and fails at the first mismatch.
(`keyword_p()` in `smdlisp/reader/atom.hpp` is a Lisp `:foo` keyword-datum
reader — an unrelated concept that happens to share the name.) Writing one in
the existing char-at-a-time style is small, and it is the natural spelling for
"this grammar position admits exactly one token".

**2. A `std::from_chars`-backed checked integer parser.** `smdscheme`'s integer
atom parser is `some<20>(...)` plus a manual digit-to-int accumulation loop,
with no overflow check. specgen had the same shape in a different guise — a
`std::stoi` whose `std::out_of_range` was a latent crash on a malformed input —
and replacing it with a `from_chars`-backed `digits()` that reports overflow as
a *positioned parse failure* turned a throw into an ordinary diagnostic. The
same substitution applies here, and `from_chars` does the range check for free.

**3. Documentation: ordered choice commits on a shared prefix.** `operator|`
falls through to its right alternative only when the left fails *before
consuming input* — correct, and the right semantics. What is easy to miss is
what it implies for alternation over spellings that share a prefix. Every marker
in specgen's docblock grammar begins with `\`, so

```c++
keyword("\\pre") | keyword("\\post") | keyword("\\at")
```

commits on the shared backslash and can never reach the second alternative once
the first candidate's *second* character mismatches. The fix is not a change to
the library — it is to scan the identifier once and dispatch the resulting name
through a table. Worth a sentence in the `operator|` documentation, because the
failure is silent: the grammar simply stops recognizing the later alternatives.

Recorded as OBS-3 and OBS-4 in specgen's `foundation/DIVERGENCES.md`, the second
pinned by a negative test.

---

## Draft 4 — transpose

**Title:** Usage report: the explicit-parameter tier was sufficient for a real tool

**Body:**

Feedback, not a defect, offered because the two-tier structure describes
its lookup tier as a temporary P3200 stub and it is presumably useful to know
how the other tier holds up on its own.

[`specgen`](https://github.com/steve-downey/specgen) — a Clang-based generator
for C++ standard-library specification wording — adopted the `traverse`-shaped
validation and monoidal-diagnostics *ideas* from this repo, and took only the
**explicit-parameter** tier: every verb receives its operations as ordinary
arguments (`mconcat(range, monoid{combine, identity})`, `fold_with` given its
`fmap`/`project` directly). No CRTP typeclass base, no `*_typeclass` lookup, no
`Foldable`/`Traversable` specialization.

It was sufficient, and not by a narrow margin. The reason looks like scale: the
tool has on the order of four functor-shaped types and ten call sites over them.
At that size an explicit `{combine, identity}` pair passed by hand costs less
than a lookup framework, and it reads at the call site without a level of
indirection. The diagnostics monoid is now a production consumer (a validator
folding over an IR tree), so this is not a toy measurement.

The part that may be useful upstream: **nothing in the adoption wanted the
lookup tier**, so if the stub is a maintenance cost, it is not one this consumer
would notice being deferred. Moving an explicit call site to a lookup later is
mechanical, which is what made waiting free.

Recorded as OBS-2 in specgen's `foundation/DIVERGENCES.md`, and as its
no-typeclass-objects decision.
