# Extraction and outline plan — the rest of the tree-algorithms work

Date: 2026-07-13.
Scope: what remains to extract from `~/src/trees/main`, how the pieces
distribute across the coordinated proposal set, and the outline delta for
the Paper D draft (now `papers/algorithms-for-trees.md`;
`DnnnnR0` at the time this plan was written).
Sources surveyed for this plan: `trees/main` (origin/pedagogy repo,
including `docs/notes/standardization-inverted-triangle-plan.md`),
`transpose/transpose` (beman.transpose, D3200R0), `fixpoint/main`
(smd::fixpoint evidence repo), `fingertree/main` (beman.fingertree,
Paper C), and this repository.

## 1. The coordinated set — who owns what

| Piece | Repo | Paper | Owns |
|-------|------|-------|------|
| Traversal, transposition, bundled customization | `~/src/transpose/transpose` | P3200 / D3200R0 (Paper A) | The mechanism: typeclass-object lookup, `Functor`/`Foldable`/`Applicative`/`Traversable`/`Monad` CRTP bases, `traverse`, `transpose`, `invoke`, `Monoid` customization point |
| Persistent measured sequence | `~/src/fingertree/main` | Paper C (unnumbered) | The container: FingerTree, measures, monoid-tagged split/search, allocator/PMR story |
| Recursive tree algorithms | this repo | Paper D (`D4322R0`) | The verbs: `fold_fix`/`unfold_fix`/`refold`, `Fix`/`Box` as exposition, adapters proving the verbs across representations |
| Recursion-scheme catalog (evidence only) | `~/src/fixpoint/main` | none — deliberately unproposed | para/apo/zygo/histo/futu/…, distributive laws, gcata/gana/ghylo; the proof that the three proposed verbs are the stable primitive layer |
| Origin / pedagogy | `~/src/trees/main` | CppNow26 talk | The source everything above was extracted from; extraction is one-way |

Division-of-labor rules already frozen (DECISIONS.md, Decision 1 and 4):
Paper D **consumes** the P3200 mechanism in a separate convenience header
and never respecifies it; the explicit-`fmap` primary API has no
customization-mechanism dependency at all; no normative dependence in
either direction between Papers C and D.

## 2. Gap analysis

Already in this repo (the R0 minimal core): `Fix`/`Box`, the three verbs
(explicit-fmap and lookup forms), functor-only typeclass lookup,
`ExprF` expression example, `BinaryTree` adapter via `to_fix`/`from_fix`,
article + paper with transcluded anchors, 53/53 tests across 8 toolchains.

Not yet extracted, outlined, or designed — the subject of this plan:

- **A. The fold family** — `fold_map` + monoid, proposed *here* (D has
  the slack; see §3 for the revised ownership analysis).
- **B. Traversable explanation** — how P3200's `traverse`/`transpose`
  fit the tree story; prose + example, never respecification.
- **C. Direct verbs for user tree types** — folding a nonce binary tree
  or fringe tree *without* converting to `Fix`. This is the only item
  requiring genuinely new API design.
- **D. Fringe tree** — second non-Fix representation; the bridge to
  measures and Paper C vocabulary.
- **E. Companion-paper fit** — expanded relationship text in the paper.
- **F. Fixpoint compatibility** — checklist ensuring nothing added here
  blocks upgrade to the unproposed catalog.
- **G. Out-of-band relationships piece** — the "missing Paper B"
  acknowledgment and the map of how the coordinated set fits together,
  published outside the paper track (§7a).

A principle that governs items A and G, worth stating once:
**repo surface ≠ paper surface.** A beman repo may legitimately ship
more than its paper proposes (beman.transpose ships `fold.hpp` and
`monad.hpp`; D3200R0 mentions neither), but every unproposed component
must be explicitly labeled evidence, the way the fixpoint catalog is for
Paper D. Silence plus shipped code is the configuration to avoid.

## 3. Work item A — the fold family (proposed in D)

**Ownership decision (revised 2026-07-13, ratified by Steve).** An
earlier draft of this plan said "Paper A owns the Foldable surface."
That conflated repo surface with paper surface: beman.transpose *ships*
`fold.hpp` (`Foldable<Impl>` CRTP base, `foldable_typeclass<T>`,
`fold_map` plus ten derived operations) and `monoid.hpp`, but D3200R0
never mentions Foldable — it is proposal-orphaned there, and nothing in
the three-domain transpose proof needs it. Its motivation is
tree-shaped: for flat ranges `std::ranges::fold_left` already exists,
and the only honest answer to "why `fold_map` when `fold_left` exists?"
is "structures that aren't ranges" — exactly what Paper A keeps out of
its motivation and exactly what Paper D is about.

**So Paper D proposes the fold family, both structural and elementwise**:
`fold_fix` (already in) plus `fold_map` as user-facing API. Paper A keeps
the *mechanism* (CRTP bases, lookup objects) and proposes only what its
examples force (Applicative, Traversable); its `fold.hpp` remains repo
surface — evidence the mechanism scales, not proposed wording.

Deliverables here:

1. **`fold_map` as proposed API** with an explicit-monoid primary
   spelling mirroring the explicit-`fmap` rule: the primary form takes
   the combine operation and identity (or a monoid object) explicitly,
   so it works with no P3200 adoption; lookup-based overloads resolving
   `foldable_typeclass`/`monoid_v` live in the convenience header, which
   remains an explicit P3200 consumer. Keeps Decision 4's independence
   invariant intact — the mechanism debate still happens only in P3200.
2. **Instances** for the tree representations shipped here (`Fix<ExprF>`,
   `BinaryTree`/`BinaryTreeF`, fringe tree once ported).
3. **The derivation**: `fold_map` over any `Fix<F>` is itself a
   `fold_fix` whose algebra combines one layer through the monoid. One
   generic instance covers every Fix-based tree with a Functor instance.
   That derivation belongs in the paper — it is the concrete demonstration
   that the three verbs are the primitive layer and element-wise folding
   is derived, not parallel machinery.

**Coordination consequence for Paper A** (transpose-repo change, needs
Steve there): A's `Traversable` must **not** require Foldable as a
superclass — C++ concepts don't need Haskell's hierarchy, `traverse`
alone suffices, and `foldMapDefault`-style derivations can be cited as
compatibility evidence. Otherwise D proposing Foldable "beneath" an
already-reviewed Traversable later creates a dependency inversion.

**Source material**: `trees/main/src/smd/typeclass/foldable.hpp`,
`monoid.hpp`, `src/smd/typeclass/examples/foldable_examples.cpp`, and the
Foldable part of `foldable-applicative-traversable.org` (Part 5). The
punch list already earmarks "Foldable/Traversable article material
(source lines 572–796)" as a follow-up — this work item is that item.

**Test rules**: DEV-01 applies with extra force — monoid-only folds are
exactly the shape DEV-01 warns about (commutative monoids can't
distinguish traversal orders). Every Foldable test needs at least one
non-commutative monoid (string concatenation is the canonical pick) so
in-order versus reversed traversal produces different answers. Traversal
order is contractual (in-order for binary trees; document it per
instance).

## 4. Work item B — Traversable: explanation, not respecification

`traverse` and `transpose` are P3200's public identity; the tree-side
story Paper D should tell, in prose plus one worked example:

- Foldable collapses a tree to a summary; Traversable rebuilds the same
  shape while effects accumulate in an applicative context;
  `transpose` is `traverse` with identity.
- Trees are the "structure" axis of P3200's story; optional/sender/simd
  are the "context" axis. A tree of `optional<T>` transposing to an
  `optional<Tree<T>>` (all-or-nothing validation preserving shape) is the
  motivating example — e.g. validating an expression tree into
  `optional<Fix<ExprF>>`.
- Like `fold_map`, `traverse` for any Fix-based tree is derivable through
  the layer Functor — worth stating in the paper, with the example code
  living in `examples/`, not in `include/` (it consumes P3200 surface
  that this repo does not vendor).

**Coordination hazard**: beman.transpose already carries its own
`examples/binary_tree.hpp` with Foldable/Applicative/Traversable
instances — a second, independently evolving port of the same
`smd::tree::BinaryTree`. Neither repo should treat the other's copy as
authoritative; both derive from `trees/main`. Any change to the
BinaryTree example semantics (e.g. traversal order) must be checked
against the other repo's copy. (Recorded in the coherence skill.)

## 5. Work item C — Direct verbs for user tree types (new design work)

**The gap.** Today a non-Fix tree reaches the verbs only through
`to_fix`/`from_fix`, which deep-copy the whole structure both ways. The
adapter chapter is honest about that cost, but "convert your tree to our
tree first" is a weak answer for the paper's own thesis that the
algorithms should work across representations users already have.

**The design insight — refold already is the direct fold.** A direct
fold over a user tree needs exactly one extra ingredient: a *project*
function exposing one layer of the user's representation as a base
functor value, `project : Tree -> F<Tree>` (Haskell's `Recursive`
class). But `refold(algebra, coalgebra, fmap_fn, seed)` with
`coalgebra = project` *is* that direct fold — no `Fix` is ever
materialized. Dually, a direct unfold needs
`embed : F<Tree> -> Tree` and is `refold` with `embed` as the algebra.
And `fold_fix` itself is the degenerate case where
`project = unwrap_fix`. So the direct tools are thin, teachable wrappers
over the already-proposed primitive, not new machinery:

```cpp
// Candidate shape (naming open — see below):
// project : Tree -> F<Tree>          (one layer, by value)
// fold_tree<Result>(algebra, fmap_fn, project, tree)
//   ≡ refold<Result, F>(algebra, project, fmap_fn, tree)
// unfold_tree<Tree>(coalgebra, fmap_fn, embed, seed)
//   ≡ refold<Tree, F>(embed, coalgebra, fmap_fn, seed)
```

Equivalence laws come free and match the fixpoint repo's law style:
`fold_tree(alg, fmap, unwrap_fix, t) ≡ fold_fix(alg, fmap, t)`, and the
`BinaryTree` adapter's `to_fix`-then-`fold_fix` must equal
`fold_tree` with a `project` that reads the `shared_ptr` children
directly (the existing order-sensitive shape algebra is the right test).

**Naming decided (Steve, 2026-07-13): `fold_with` / `unfold_with`** —
fold/unfold *with* an explicitly supplied projection/embedding. Decision
2 constraints respected: not `recursive_fold` (prose, not identifier),
not `*_fix` (these verbs pointedly do not go through `Fix`). The reading
coheres with the transpose repo's `*_with` convention understood as "the
ingredient you'd otherwise look up is passed inline" (there a typeclass
instance, here the projection); document that reading in both papers so
the shared suffix is a family resemblance, not a collision. The
fingertree extraction memo's penciled `recursive_fold.hpp`/
`recursive_build.hpp` refer to this design and should be renamed.

**Implemented 2026-07-13** in `recursion_schemes.hpp` (anchored), with
equivalence-law static_asserts (`fold_with ∘ unwrap_fix ≡ fold_fix`,
`unfold_with ∘ wrap_fix ≡ unfold_fix`), nonce-tree runtime tests
(DEV-01 shape + decrement discriminators, move-only `unique_ptr`
carrier build via `unfold_with`), the adapter-vs-direct agreement law in
`binary_tree.test.cpp`, and `examples/nonce_tree_direct.cpp`.

**Prerequisite check.** `project` returning `F<Tree>` by value copies one
layer per node (value semantics, consistent with Decision 6). The
fixpoint repo explicitly declared `Recursive`/`Corecursive` typeclasses
a non-goal *there* (§11) — the design work is unclaimed and lands here.
Compatibility requirement: whatever shape `project`/`embed` take must be
usable as the coalgebra/algebra of the *generalized* schemes in the
fixpoint catalog (gana/gcata take arbitrary coalgebras/algebras, so a
plain callable satisfies this — keep it a plain callable, not a new
typeclass, for R0).

**The two demonstration trees:**

1. **Nonce binary tree** — the paper's "tree you wrote yesterday":
   `struct Node { int v; std::unique_ptr<Node> l, r; };` Three
   ingredients (a base-functor struct, an `fmap`, a `project`) get the
   whole verb family with no conversion and no P3200 dependence. This
   becomes the new centerpiece of the adapter chapter; the existing
   `to_fix`/`from_fix` conversion story remains as the "when you also
   want the Fix form" variant.
2. **Fringe tree** (work item D) — port `trees/main/src/smd/tree/fringe_tree.hpp`:
   variant-based `Empty | Leaf | Branch`, values at leaves, branches
   carry a cached measure (leaf count), O(1) concatenation via sharing.
   Exercises: a variant base functor with more than two alternatives,
   values-at-leaves (versus BinaryTree's value-at-every-node), and a
   cached-measure invariant that `embed` must maintain — which is exactly
   the measures/monoid vocabulary of Paper C, making the fringe tree the
   in-paper bridge to the persistent-measured-sequence story without any
   normative dependence.

## 6. Work item E — Fingertree fit (prose, no code dependence)

No normative dependence in either direction; keep it that way. What the
paper should say, and what must stay aligned:

- The FingerTree of Paper C is "one more recursive representation the
  verbs can serve" — with the fringe tree's cached measure as the
  in-paper miniature of the same idea (monoid-tagged structure).
- **Shared vocabulary to keep aligned across repos**: `Monoid` spelling
  (`identity()` / `combine()` — both transpose and fingertree ship this;
  any change is a three-repo sweep), measure-as-functor
  (`T -> Tag`), and the typeclass-object lookup pattern.
- FingerTree's current Traversable instance materializes via
  `flatten()`/`from_sequence()` — O(n), not structure-preserving. If
  direct verbs (item C) make a structure-preserving path possible later,
  that's an upgrade behind Paper C's API, not a Paper D obligation.
  Worth one sentence in the companion-papers section.

## 7. Work item F — Fixpoint compatibility checklist

Everything added by items A–D must keep this repo upgradeable to the
unproposed catalog (the astronautics are evidence that the core is the
stable primitive layer — new surface must not falsify that claim):

- Direct verbs stay plain callables over `refold` — usable as
  gana/gcata coalgebras/algebras unchanged.
- Foldable instances derive through `fold_fix`; nothing precludes
  carrier-based schemes (para/histo need pairing and Cofree carriers —
  the layer Functor instance is the only hook they need, and it exists).
- DEV-01 (non-idempotent, order-sensitive test algebras; for folds:
  non-commutative monoids), DEV-03 (no bare CTAD on wrapped types),
  DEV-04 (constexpr at declaration + static_assert coverage) apply to all
  new code.
- Every new verb or instance gets an equivalence law pinning it to an
  already-tested primitive (`fold_tree ∘ unwrap ≡ fold_fix`,
  `fold_map ≡ fold_fix ∘ monoid-algebra`, adapter-vs-direct agreement).
- Language floor stays C++23 / gcc-14+ / clang-19(18?)+ — do not import
  fixpoint-repo C++26-isms (its D9 gate is for the catalog campaign).

## 7a. Work item G — the out-of-band relationships piece ("Paper B")

The coordinated set has a deliberate hole where Monad would sit — the
A/C/D lettering even leaves the slot open. Decision (Steve, 2026-07-13):
acknowledge it explicitly, but out-of-band rather than as a WG21 paper.
A published essay explains the relationships once, and each paper cites
it instead of re-explaining the map.

**Content of the essay:**

- The map: A = mechanism + Applicative/Traversable (the independence
  axis); C = concrete persistent measured sequence; D = the fold family,
  structural and elementwise; fixpoint catalog = evidence that the
  proposed verbs are the stable primitive layer.
- Why Monad is deliberately unproposed: `bind` is sequential, and the
  motivating domains are precisely the ones sequential composition
  cannot serve (SIMD lanes have no per-lane control flow; batched
  senders transpose *because* members are independent); C++ already
  chose member spellings for monadic composition (`and_then`/`or_else`/
  `transform` on `optional` per P0798 and on `expected` per P2505,
  `let_value` on senders), and a Monad typeclass would read as
  respecifying adopted facilities. `monad.hpp` in beman.transpose is
  the existence proof that the mechanism accommodates Monad if anyone
  ever wants the paper — the slot is open, not forgotten.
- The repo-surface ≠ paper-surface principle (§2), so reviewers reading
  the repos don't mistake shipped evidence for proposed wording.

**Venue**: sdowney.org (precedent: "Concept Maps Using C++23 Library
Tech," already cited by this paper), and/or a sixth entry in transpose's
`papers/blog/` series. Cross-cutting, so draft wherever convenient but
publish where all three papers can cite one stable URL.

**Paper hooks** (small, per-paper):

- D3200R0 gains a short "Why not Monad" section making the argument in
  its own text — the obvious LEWG question should land on prepared
  ground, the same costs-stated-by-authors review control D already
  practices — plus a citation of the essay.
- This paper's "Relationship to companion papers" cites the essay and
  carries a one-sentence Monad acknowledgment (see §8).
- Paper C cites the essay from its companion section when drafted.

## 8. Paper outline delta for the D paper (now D4322R0)

New/changed sections, in document order:

1. **Design overview** gains a short subsection after "Typeclass lookup":
   *"Trees you don't own"* — project/embed, direct verbs as `refold`
   spellings, forward reference to the adapter chapter.
2. **New section: "Folding and traversing trees"** (after the API
   synopsis): `fold_map` as proposed API — explicit-monoid primary
   spelling, the derivation from `fold_fix`, traversal-order contract,
   and the "why not `fold_left`" answer (structures that aren't ranges);
   then the Traversable prose of item B with the validation example.
   Explicit statement: mechanism stays in P3200; the fold family is
   proposed here.
3. **Proposed API synopsis** grows the direct verbs (item C) and the
   explicit-monoid fold spelling (item A.3), each with anchored regions
   in the headers (extend `docs/notes/anchor-inventory.md`; re-diff rule
   from the synopsis HTML comment applies — and note `unfold_fix`/`refold`
   explicit-fmap overloads are currently unanchored, carried on the punch
   list).
4. **Implementation experience** gains the fringe tree and nonce-tree
   evidence (three representations become four).
5. **Non-goals** gains: no `Recursive`/`Corecursive` typeclass — the
   project/embed hooks are plain callables in R0.
6. **Relationship to companion papers** expands per items B and E, cites
   the out-of-band relationships essay (item G), and carries a
   one-sentence acknowledgment that Monad is deliberately unproposed
   across the set, with the essay as the reference.
7. **Naming** gains the direct-verb naming table once decided, and the
   `fold_map` naming rationale (kept as-is: cross-language consensus
   name, and the elementwise/structural contrast with `fold_fix` is the
   section's teaching device).

## 9. Sequencing

1. **C first** (direct verbs) — smallest header delta, unlocks the
   nonce-tree example and reframes the adapter chapter; naming decision
   from Steve is the only blocker, and implementation can proceed under a
   placeholder name in a branch.
2. **D** (fringe tree port) — exercises the direct verbs on a second
   representation; measures bridge.
3. **A** (the fold family) — `fold_map` proposed API + instances +
   derivation; depends on nothing above but reads better in the paper
   after the direct verbs exist (instances can be written for both Fix
   and direct trees at once).
4. **B, E** (paper prose: traversable + fingertree fit) — after A so the
   Foldable/Traversable contrast is real in this repo.
5. **G** (out-of-band essay + per-paper hooks) — can be drafted any time;
   publish before any paper cites it, so before the §8 paper-edit pass.
6. **§8 paper edits + anchor/synopsis maintenance** — last, one pass.

Decisions needing Steve (beyond the existing punch-list items):

- Direct-verb names (§5).
- Whether the explicit-monoid fold spelling ships as
  `(monoid, tree)` or `(combine, identity, tree)` — the latter avoids
  even a Monoid trait in the primary API.
- Whether the fringe tree lives in `include/` (proposed surface
  evidence) or `examples/` (demonstration only). Recommendation:
  `include/` next to `binary_tree.hpp` — it is adapter evidence of the
  same standing.
- Venue and timing for the relationships essay (§7a), and making the
  matching transpose-repo changes there: the "Why not Monad" section in
  D3200R0, labeling `fold.hpp`/`monad.hpp` as unproposed evidence, and
  keeping Traversable free of a Foldable superclass requirement (§3).

Resolved 2026-07-13 (Steve): the fold family is proposed in Paper D, not
Paper A; the Monad slot ("Paper B") is acknowledged explicitly but
out-of-band, not as a WG21 paper.
