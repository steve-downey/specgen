# DECISIONS.md — Frozen Decision Record for `beman::tree_algorithms`

This document records the already-ratified design decisions governing the `tree_algorithms` repository, the Beman-project implementation surface for Paper D ("Recursive Tree Algorithms for the Standard Library").
Each decision was made and tested elsewhere — chiefly in the `smd::fixpoint` evidence repository — and is recorded here as frozen input to this repo, not as an open question.
Do not relitigate these in code review; a change to any of them requires updating this file first.

Source documents cited below:

- `docs/recursion-schemes-design.md` (fixpoint repo) — decisions D1, D4, D5, D6, D10 and the §11 non-goals list.
- `ops/DEVIATIONS.md` (fixpoint repo) — deviation ledger DEV-01 through DEV-04.
- `src/smd/fixpoint/box.hpp` (fixpoint repo) — the hand-rolled constexpr `Box` and its `std::indirect` rationale comment.
- `src/smd/fixpoint/recursion_schemes.hpp` (fixpoint repo) — the canonical verb signatures.
- `src/smd/fixpoint/fixpoint-trees-in-cpp.org` (fixpoint repo) — the "A Note on Names" and "Composition, not inheritance" sections.
- `src/smd/fixpoint/replacing_vist.md` (fixpoint repo) — the "Engineering Reality" cost discussion.
- `docs/proposal-strategy.org` (trees repo) — the Paper D scope and the Paper A / P3200 division of labor.

## Decision 1 — Scope: minimal algorithms core only

This repository contains exactly the minimal core Paper D proposes: `Fix`/`Box`, the three verbs `fold_fix`/`unfold_fix`/`refold`, functor-only typeclass lookup, and tree adapters demonstrating the verbs over more than one recursive representation.
Nothing else.

The exotic recursion schemes — para, apo, zygo, mutu, prepro, postpro, histo, futu, dyna, codyna, chrono, mcata, mhisto, elgot, coelgot, the distributive laws, and the generalized gcata/gana/ghylo family — remain in the fixpoint repository as implementation evidence and are deliberately out of scope here.
This follows the Paper D framing in `proposal-strategy.org`: "The fixpoint machinery is evidence and implementation support, not the public identity of the proposal," and the paper's core claim is "a family of verbs — reduce, fold, rebuild, build — not one blessed tree type."
The fixpoint design document confirms the extended catalog is a separate campaign with its own plan, deviations ledger, and gates (`recursion-schemes-design.md` §1, §11).
Consequence for the deviation ledger below: DEV-02 (distributive laws must be variable templates on `F`, and `dist_apo`/`dist_gapo` need explicit element-type arguments) is recorded as **N/A for this repository**, because no distributive law ships here; it is retained in the fixpoint repo's ledger where the code lives.

## Decision 2 — Naming: `fold_fix`, `unfold_fix`, `refold`; never `cata`, `ana`, `hylo`

The public identifiers are `fold_fix`, `unfold_fix`, and `refold`.
The literature names `cata`, `ana`, and `hylo` are never public names in this repository — not even as deprecated aliases.
"Recursive fold" and "recursive build" are prose descriptions used in the paper and documentation; they are not identifiers.

Rationale, per fixpoint design decision D1: "The existing library deliberately renamed cata/ana/hylo to `fold_fix`/`unfold_fix`/`refold` (and `[[deprecated]]`-ed `cata`). … Do not resurrect `cata`."
The "A Note on Names" section of `fixpoint-trees-in-cpp.org` gives the underlying reasoning: "The traditional names for these three schemes — catamorphism, anamorphism, hylomorphism — are precise, but they are jargon that most C++ programmers have no reason to know," and cites the Haskell `data-fix` 0.3.4 precedent (`foldFix`/`unfoldFix`/`refold`) for the same conclusion.
The fixpoint repo still carries a `[[deprecated]] cata` shim for its own history; this repo starts clean and does not port it.

## Decision 3 — Box: hand-rolled constexpr `Box`, not `std::indirect`

We ship the hand-rolled `Box<A>`: a constexpr-capable, nullable, deep-copying owning pointer, aggregate-initialization friendly, using raw `new`/`delete` (constexpr for transient allocations in C++20+), with `make_box<A>(args...)` as the factory.
Consequence: the language floor is C++23, recorded in the copier answers as `minimum_cpp_build_version=23`; the real toolchain floor is deducing-this support in the typeclass lookup machinery, i.e. gcc-14+ / clang-18+.

We acknowledge plainly that `std::indirect` has the right semantics for this job, modulo one blocking detail.
The `box.hpp` rationale comment is the authoritative statement: "This is a workaround until consteval allocation lands in a future standard; std::indirect has the right semantics but its explicit default constructor blocks use in aggregate-initialised containers."
Fixpoint design decision D10 ratifies staying with `Box` even where `std::indirect` is available: "`std::indirect` exists in GCC 16's C++26 library and could replace `Box`; we deliberately stay with `Box` for consistency and its nullable default (see box.hpp's rationale). Migration to `std::indirect` is out of scope (§11)."
For this repository the additional consideration is portability: depending on `std::indirect` would raise the floor to C++26 library support, defeating the C++23 target; the paper should present `Box` as an exposition-level implementation detail and `std::indirect` as the plausible future spelling.

## Decision 4 — API shape: explicit `fmap` in the primary verbs, lookup in a separate convenience header

The primary verbs take the base functor's `fmap` explicitly as a callable parameter, exactly as in the canonical signatures of `recursion_schemes.hpp`:

- `fold_fix<Result>(algebra, fmap_fn, tree)`
- `unfold_fix<F>(coalgebra, fmap_fn, seed)`
- `refold<Result, F>(algebra, coalgebra, fmap_fn, seed)`

Result carriers are leading explicit template parameters, per fixpoint design decision D5: "C++ cannot infer fold carriers through recursive calls the way Haskell infers types. Following the existing `fold_fix<Result>(...)` convention, every scheme takes its carrier(s) as leading explicit template parameters."
Alongside these, `Fix<F>`-only overloads exist that drop `fmap_fn` and resolve `fmap` through `functor_typeclass<T>` lookup, mirroring the lookup-based overloads of design §6.2 and the second half of `recursion_schemes.hpp`.

The lookup-based overloads live in a **separate convenience header**, and that header is explicitly a *consumer stub* of the Paper A (P3200) bundled-customization facility.
Paper D consumes the lookup mechanism; it never respecifies it.
This division follows `proposal-strategy.org`: the bundled-customization mechanism is "material that should stay mostly in Paper A," Paper D's integration point is limited to "adaptation and traversal integration where useful," and the coordinated-set rule is to "avoid hard normative dependence where possible" — the explicit-`fmap` primary verbs are the surface that remains fully valid even if Paper A does not progress at the same pace.

## Decision 5 — Composition, not inheritance

`Fix<F>` holds its layer as a data member — `F<Fix<F>> inner` — with `wrap_fix`/`unwrap_fix` as the explicit isomorphism.
It does not use the Niebler-style formulation `struct Fix : F<Fix<F>> {}`.

The "Composition, not inheritance" section of `fixpoint-trees-in-cpp.org` is the ratifying argument: "Inheritance implies substitutability: any code expecting an `F<Fix<F>>` would accept a `Fix<F>`. That is not a property the math requires, and it can create ambiguity in overload resolution or template instantiation."
The has-a formulation "says only what is true: a `Fix<F>` *contains* an `F<Fix<F>>`," the isomorphism stays explicit through `wrap_fix`/`unwrap_fix`, and the wrapping is zero cost.

## Decision 6 — Value semantics; performance tuning is a non-goal

Layers are held and passed by value; `Box` deep-copies; copies are accepted.
There is no sharing, no move-optimization pass, and no memoization.

This is fixpoint design decision D6 verbatim in spirit: "Value semantics; copies are accepted. … No sharing, no move-optimization pass, no memoization beyond what the scheme itself provides. Performance work is a non-goal (§11)."
The paper will be candid about the real costs, drawing on the "Engineering Reality" discussion in `replacing_vist.md`: heavy reliance on fixpoint combinators and recursive template instantiation taxes compile time ("the overhead of instantiating these deeply nested template structures across a large codebase can significantly degrade build latency"), and codegen depends on the optimizer inlining the generic traversal, with pointer-chasing and code-bloat risk when it does not.
Candor here is deliberate review-control: costs stated by the authors cannot be discovered by reviewers.

## Decision 7 — Testing rules

Tests use Catch2, ported near-verbatim from the existing fixpoint `.t.cpp` suites.
Three rules from the fixpoint deviation ledger are promoted to standing policy in this repository:

- **DEV-01 principle — non-idempotent, order-sensitive algebras.**
  Every `fold_fix`/`unfold_fix`/`refold` test must use algebras for which a wrong traversal order or wrong structural handling produces a wrong answer.
  DEV-01 demonstrated empirically that a monotone, idempotent test function (take-while) failed to distinguish a correct implementation from a plausibly wrong one, while a non-idempotent one ("decrement every head by 1" on `[10,10,10]`: correct 27, wrong 28) did discriminate.
  Idempotent or commutative-monoid-only test algebras cannot be the sole correctness gate for any verb.

- **DEV-03 — no bare CTAD on wrapped types.**
  `Fix{x}`, `Box{x}`, and `Identity{x}` are banned; always name the full type (`Identity<std::remove_cvref_t<decltype(x)>>{x}` and equivalents).
  DEV-03 documents the hazard: CTAD's copy-deduction candidate "silently collapses a 'wrap one level deeper' intent into 'copy' whenever the element happens to already be an instance of the same wrapper template" — a general C++ hazard that recurs whenever generic code is instantiated with an already-wrapped element type.

- **DEV-04 — constexpr on every derived operation, enforced by static_assert.**
  Every CRTP-derived operation in typeclass base classes is marked `constexpr` at declaration time, not just the `Impl`-provided primitives, and each verb is covered by at least one `static_assert` test evaluating it at compile time.
  DEV-04 documents why declaration-time discipline is required: the missing `constexpr` on `Monad`'s derived operations "was invisible until a later step's `static_assert` first reached it" — the gap has no symptom until a constant-expression caller appears.

DEV-02 is N/A here per Decision 1 (no distributive laws ship in this repository).

## Decision 8 — Copier answer sheet

Recorded verbatim for repository generation from `steve-downey/exemplar` at the `copier-extended` branch/tag:

```text
template               = steve-downey/exemplar @ copier-extended
project_name           = tree_algorithms
maintainer             = steve-downey
minimum_cpp_build_version = 23
paper                  = DnnnnR0 (placeholder until a number is requested)
description            = "Recursive tree algorithms: fold_fix, unfold_fix, refold over fixed-point trees"
unit_test_library      = catch2
```

## Amendment 2026-07-14 — the fold family joins the scope

Ratified by Steve in the coordinated-set review of 2026-07-13 (recorded
in the tree-coordination meta-repo, SYNC entries 1–3, and
`docs/notes/extraction-plan.md` §3/§5); this amendment supersedes the
scope sentence of Decision 1 and extends Decision 4's pattern.
Everything else in Decisions 2–8 stands unchanged and applies to the new
code.

- **Scope**: in addition to the minimal core, this repository ships the
  fold family: the direct verbs `fold_with`/`unfold_with`
  (projection/embedding wrappers over `refold`; no Fix materialized) and
  the elementwise `fold_map`, whose primary spelling takes the combine
  operation and identity as plain explicit parameters — no Monoid trait
  in the primary API, mirroring the explicit-`fmap` rule.
- **Naming**: `fold_with`/`unfold_with` — fold/unfold *with* an
  explicitly supplied projection/embedding. Decision 2's constraints
  were respected in the selection (not `recursive_*`, not `*_fix`, no
  literature jargon).
- **Lookup**: the consumer-stub surface grows from functor-only to three
  lookup objects — `functor_typeclass<Layer>`,
  `layer_fold_typeclass<Layer>`, and `project_typeclass<Tree>` — all
  following the same variable-template pattern, still a consumer of the
  Paper A facility, never a respecification.
- **Representations**: the fringe tree ships in `include/` as the second
  adapter — direct-only (no Fix form, no Box; children held by value),
  with its embedding maintaining the cached-measure invariant through
  `branch()`.
- **Rationale for the fold_map ownership**: `fold_map`'s motivation is
  structures that aren't ranges (`ranges::fold_left` covers flat
  sequences), so the fold family is Paper D material; beman.transpose's
  `fold.hpp` remains unproposed evidence in Paper A's repo.

## Amendment 2026-07-17 — paper number assigned

The `DnnnnR0` placeholder anticipated by Decision 8 ("placeholder until
a number is requested") is resolved: the paper is **D4322R0**, title
"Algorithms for Trees" (`papers/algorithms-for-trees.md`,
superseding `papers/DnnnnR0-recursive-tree-algorithms.md`, which is
removed).
Decision 8's answer sheet stays as recorded — it is a verbatim
generation-time record, not a live pointer.

## Amendment 2026-07-18 — storage at the knot: `child_slot` inline layers

Ratified by Steve in the benchmarking review of 2026-07-18, on evidence from the `benchmarks/` suite and an exact allocation-count diagnostic; this amendment narrows the "performance work is a non-goal" clause of Decision 6 and extends Decision 3.
Everything else stands: value semantics, `Box` deep-copies at the knot, copies accepted.

- **The finding**: indirection in a base functor is required only at the knot — `F<Fix<F>>` holds children of an incomplete type.
  Every *materialized* layer (`F<Result>` handed to an algebra, `F<Seed>` from a coalgebra, `F<Handle>` from a projection) instantiates `F` at a complete type, where children can live inline.
  A layer that hardwires `Box` pays one heap allocation per engaged child per node in every such layer; measured on a 2^20-node fold, that allocation was the entire difference between `fold_fix` and hand-written recursion (8.27 ms vs 4.64 ms, and exactly 0 allocations for the inline path vs one per engaged child boxed).
  With inline layers, `fold_fix` sits on the hand-recursion floor (4.656 ms vs 4.642 ms), `unfold_fix` allocates exactly its product, and `refold` allocates nothing (1.18 ms → 179 µs).
- **The mechanism**: `child_slot<A>` (`child_slot.hpp`) — `Box<Fix<F>>` exactly at the knot, matched by partial specialization against `Fix`'s declaration; `std::optional<A>` at every complete type; `make_slot<A>(args...)` constructs whichever the position requires.
  Both storages are nullable, test engaged as bool (`Box` gained `explicit operator bool`), and dereference with `*`, so layer code reads identically either way.
  `BinaryTreeF`, `ExprF` (`Add`/`Mul`), and `NatF` (`Succ`) hold children in `child_slot_t<A>`; fmap is unchanged in shape — `F<A> -> F<B>`, the same template, functor laws on the nose.
- **Value-category overloads**: `wrap_fix` gains an rvalue overload (one move for xvalue arguments instead of two) and `unwrap_fix` an rvalue overload returning `F<Fix<F>>&&` for consuming callers; the const-lvalue forms remain.
- **Decision 6 as amended**: value semantics and the no-sharing/no-memoization stance are unchanged, and candor about costs remains the rule.
  What changes is the claim that performance work is out of scope: storage choice in materialized layers is *design* — indirection where the math requires it and nowhere else — not tuning, and the paper should state the measured consequence: the verbs are zero-overhead against hand recursion over the same representation.
- **Decision 3 as amended**: `Box` remains the knot storage, for the recorded reasons (constexpr, nullable, aggregate-friendly).
  The inline slot is plain `std::optional`; no new vocabulary type is introduced.

## Discrepancies

Points where a cited source does not line up exactly with the decision as recorded, noted rather than silently adjusted:

- **Decision 4 citation of D5.**
  Fixpoint D5 ratifies only the *explicit carrier template parameter* convention (`fold_fix<Result>(...)`); it does not itself discuss the explicit-`fmap` parameter.
  The explicit-`fmap` convention is ratified instead by the canonical signatures in `recursion_schemes.hpp` and by design §2 ("All take fmap explicitly as a callable"); Decision 4 cites both sources for their respective halves.

- **Decision 2 vs. the fixpoint repo's shipped code.**
  `recursion_schemes.hpp` still contains a `[[deprecated("use fold_fix")]] cata` alias, and "A Note on Names" says "the old name `cata` is still available as a `[[deprecated]]` alias."
  This repository intentionally goes further than its source: no alias, deprecated or otherwise, is ported.

- **Language floor divergence.**
  The fixpoint evidence repo pins C++26 and gcc-16 (its D9); this repository targets C++23 with a gcc-14+/clang-18+ toolchain floor.
  This is a deliberate divergence, not an oversight: nothing in the extracted core requires C++26, and D9's C++26 requirement exists for the exotic-scheme campaign (C++26 library facilities and the gcc-16 gate), which is out of scope here per Decision 1.

- **`replacing_vist.md` code excerpts.**
  The essay's code uses the deprecated `cata` spelling, a lowercase `smd::fixpoint::fix` alias, and a two-argument call shape that omits the explicit `fmap_fn` and `Result` parameters of the canonical signatures.
  It is cited in Decision 6 solely for its cost analysis ("Engineering Reality"); its code excerpts are not authoritative for naming or API shape and are superseded by Decisions 2 and 4.
