---
title: "Algorithms for Trees"
document: D4322R0
date: 2026-07-17
audience: LEWGI, LEWG
author:
  - name: Steve Downey
    email: <sdowney@gmail.com>
toc: true
toc-depth: 3
---

# Abstract

Recursive tree structures are common in parsing, transformation pipelines, symbolic representations, and hierarchical data processing, but the standard library offers little direct support for generic algorithms centered on recursive structure rather than on flat external iteration.

This paper proposes a focused family of recursive algorithms over recursive node structures.
The structural verbs `fold_fix` (recursive fold), `unfold_fix` (recursive build), and `refold` (fused build-then-fold) operate over fixed-point trees; `fold_with` and `unfold_with` are the same folds over a user's tree in its own representation, through an explicitly supplied projection or embedding, with no conversion and no fixed-point wrapper materialized; and `fold_map` is the elementwise fold — map each element, combine under an associative operation — derived from the structural verbs rather than specified as parallel machinery.
The unifying theme is that recursive trees are more naturally served by structure-directed traversal and reconstruction than by cursor-oriented generic interfaces alone.

Recursive trees expose generic algorithmic structure not well captured by sequence-centric iterator models, and C++ would benefit more from reusable recursive algorithms than from prematurely standardizing one blessed tree vocabulary type.
Accordingly, this is an algorithms paper.
The fixpoint machinery used in the reference implementation is evidence and implementation support, not the public identity of the proposal.

# Before / After

| Before                                                          | After                                          |
|-----------------------------------------------------------------|------------------------------------------------|
| Recursive structures handled through bespoke one-off algorithms | One reusable algorithm family over trees       |
| Iterator-centric interfaces that expose traversal mechanics     | Structure-directed traversal or reconstruction |
| Limited reuse across different recursive tree representations   | A generic story across more than one tree type |

Concretely, evaluating a variant-based expression tree today means hand-rolling the recursion at every use site:

::: cmptable

> Before

```cpp
// One-off recursive visitation:
// std::visit plus explicit recursion,
// rewritten for every algorithm.
int eval(const Expr& e) {
    return std::visit(overloaded{
        [](const Lit& l) {
            return l.value;
        },
        [](const Add& a) {
            return eval(*a.lhs)
                 + eval(*a.rhs);
        },
        [](const Mul& m) {
            return eval(*m.lhs)
                 * eval(*m.rhs);
        },
    }, e.node);
}
```

> After

```cpp
// The recursion is the algorithm's job;
// the user supplies only the per-layer
// algebra.
int result = fold_fix<int>(
    overloaded{
        [](const Lit<int>& l) {
            return l.value;
        },
        [](const Add<int>& a) {
            return a.lhs + a.rhs;
        },
        [](const Mul<int>& m) {
            return m.lhs * m.rhs;
        },
    },
    expr_fmap, tree);
```

:::

The "After" algebra is non-recursive.
Each alternative sees children that have already been reduced to the result type.
The same tree works with a pretty-printing algebra, a depth algebra, or a rewriting algebra without touching the traversal again, and the same `fold_fix` works over any recursive representation that supplies a mapping function for one structural layer.

# Motivation and scope

## Why recursive structure deserves direct treatment

The C++ standard library has strong support for sequence-oriented generic programming centered on external iteration.
That support remains valuable, but it is not the only credible foundation for generic algorithms.

For recursive structures, a different center of gravity is often better:

- internal traversal rather than external cursor choreography,
- structural reconstruction rather than elementwise mutation,
- a small user-supplied per-layer operation rather than a hand-maintained recursive function per algorithm.

Recursive structure creates algorithmic reuse opportunities that flat iterator-centric APIs do not capture well.
Flattening a tree into an iteration order discards exactly the structure that tree algorithms are about, and every project that manipulates expression trees, document trees, or hierarchical configuration reinvents the same recursive folds and rebuild passes locally.

## What is proposed — and what is deliberately not

The missing library surface is a family of verbs — reduce, fold, rebuild, build — not one blessed tree type.

This paper proposes one small family, in three groups:

- **Structural verbs over fixed-point trees**:
  `fold_fix` — recursive fold: consume a recursive structure bottom-up with a non-recursive per-layer algebra;
  `unfold_fix` — recursive build: grow a recursive structure top-down from a seed with a non-recursive per-layer coalgebra;
  `refold` — the fusion of the two: build-then-fold without materializing the intermediate structure.
- **The same verbs over trees in their own representation**:
  `fold_with` — fold through an explicitly supplied *projection* exposing one layer of the user's tree at a time;
  `unfold_with` — build through an explicitly supplied *embedding* reconstructing one layer of the user's representation.
  Both are `refold` in disguise; no conversion happens and no fixed-point wrapper is materialized.
- **The elementwise fold**:
  `fold_map` — map each element to the result type and combine under an explicitly supplied associative operation with identity; length, sums, renderings, and any/all queries are all `fold_map`s, and its implementation *is* the derivation from the structural verbs.

This paper explicitly does **not** propose a standard tree container or tree vocabulary type.
Prematurely standardizing one blessed tree layout would be both harder to agree on and less useful than the algorithms, because the algorithms work across representations users already have.
The motivating examples in the reference implementation span five recursive-tree representations, two of which are served with no fixed-point machinery at all (see [Implementation experience](#implementation-experience)).

This paper does not depend on the persistent-sequence container paper (Paper C in the coordinated set; see [Relationship to companion papers](#relationship-to-companion-papers)).
It consumes, but does not respecify, the bundled-customization mechanism of P3200 (Paper A) in an optional convenience layer.

# Design overview

## Fix and Box: exposition-level machinery

The reference implementation ties the recursive knot with a fixed-point wrapper.
A user-supplied *base functor* `F` describes one layer of structure with a type parameter in the recursive positions; `Fix<F>` closes the recursion:

```cpp
template <template <typename> class F>
struct Fix {
    F<Fix<F>> inner;   // composition, not inheritance
};
```

`Fix<F>` holds its layer as a data member, with `wrap_fix`/`unwrap_fix` as the explicit isomorphism.
The formulation `struct Fix : F<Fix<F>> {}` was considered and rejected: inheritance implies substitutability — any code expecting an `F<Fix<F>>` would accept a `Fix<F>` — which is not a property the design requires, and which can create ambiguity in overload resolution or template instantiation.
The has-a formulation says only what is true, and the wrapping is zero cost.

Recursive positions inside base-functor alternatives are held through `Box<A>`, a constexpr-capable, nullable, deep-copying owning pointer, so that `F<X>` is a complete type even for incomplete `X`.
`std::indirect` has the right semantics for this job, and it is the plausible future spelling; the reference implementation stays with a hand-rolled `Box` because `std::indirect`'s explicit default constructor blocks use in aggregate-initialized containers, and because depending on it would raise the implementation floor to C++26 library support.
In this paper both `Fix` and `Box` are exposition-level implementation detail: they are how the reference implementation demonstrates the algorithms, not names this paper asks LEWG to bless as user-facing vocabulary.

## Building on Fix directly

Exposition-level does not mean toy.
The subsections below treat the fixed-point wrapper as an adapter target for trees that already exist; it is equally a representation a user can build on from the start, and the reference implementation includes a full working container that does: a binary search tree whose tree type is `Fix` over a binary base functor directly, with a disengaged `Box` doing double duty as both "empty tree" and "absent child" (`examples/search_tree_on_fix.cpp`).

The example's value to this paper is its honest division of labor.
`unfold_fix` builds the tree balanced from a sorted range; `fold_fix` and `fold_map` implement the whole-tree consumers — flatten, height, shape.
But insert, contains, and erase are plain structural recursion written with `wrap_fix` and `unwrap_fix`, because search *prunes*: it descends one child and ignores the other, while a fold visits everything.
The fixed-point machinery does not take ordinary recursion away, and the proposed verbs do not pretend to subsume it.
(The literature's pruning and short-circuiting schemes — paramorphisms and apomorphisms — exist in the evidence catalog precisely for such operations; they remain deliberately unproposed. See [Non-goals](#non-goals).)
The whole lifecycle — insert, search, erase, flatten — is exercised at compile time in a `static_assert`.

## Explicit `fmap`: the primary API

The primary verbs take the base functor's mapping function — `fmap`, the function that applies a callable to each child position of one structural layer — explicitly as a callable parameter.

This has three consequences:

- The primary API has **no customization-mechanism dependency at all**.
  A user with any variant-based recursive type can call the verbs today by writing one per-layer mapping function.
- Result carriers are leading explicit template parameters (`fold_fix<Result>(...)`), because C++ cannot infer fold carriers through recursive calls the way languages with global type inference can.
- The verbs are ordinary function templates, `constexpr` throughout.

`fold_map` follows the same rule at the element level: its primary spelling takes the combine operation and its identity as plain parameters — two callables/values, no Monoid trait anywhere in the primary API.

## Trees you don't own: project and embed

Adapting an existing tree by conversion — building a fixed-point copy, folding it, converting back — is honest about its cost, but "convert your tree to our form first" is a weak answer for a paper whose thesis is that the algorithms should serve representations users already have.

The direct verbs close that gap with one more explicit callable each.
A *projection* exposes one layer of the user's tree as a base-functor value, `project : Tree -> F<Handle>`, where the child handles are whatever the projection itself accepts — raw pointers into the existing structure work, so nothing is copied beyond one layer at a time.
`fold_with(algebra, fmap_fn, project, tree)` is then exactly `refold` with the projection as its coalgebra; dually, `unfold_with(coalgebra, fmap_fn, embed, seed)` is `refold` with an *embedding* `embed : F<Tree> -> Tree` as its algebra.
No `Fix` is materialized in either direction, and `fold_fix` itself is the degenerate case whose projection is `unwrap_fix` — an equivalence the test suite pins at compile time.

Because the ingredients are three small callables — a layer struct, an `fmap`, a projection — the verbs reach trees that no conversion pipeline could serve: the reference implementation's example folds and *builds* a tree whose children are held by `std::unique_ptr`, a move-only structure that cannot be copied into any other form.
The projection and embedding remain plain callables in this revision; no `Recursive`/`Corecursive`-style concept is proposed for them (see [Non-goals](#non-goals)).

## Typeclass lookup: a separate convenience layer

Alongside the explicit-`fmap` verbs, lookup-based overloads drop the structural parameters and resolve them through typeclass-object lookup in the style proposed by P3200: `fmap` through `functor_typeclass<Layer>`, the elementwise layer fold through `layer_fold_typeclass<Layer>`, and the projection through `project_typeclass<Tree>`.
The everyday spelling of the elementwise fold is then four arguments — `fold_map<Result>(map_fn, combine, identity, tree)` — over either a fixed-point tree or a user's tree in its own representation.

These overloads live in separate convenience headers, and those headers are explicitly *consumers* of the P3200 bundled-customization facility.
This paper consumes the lookup mechanism; it never respecifies it.
The explicit primary verbs are the surface that remains fully valid even if P3200 does not progress at the same pace.

# Proposed API synopsis

Namespace and header names below follow the reference implementation (`beman::tree_algorithms`) and are placeholders for LEWG naming review.

<!--
This synopsis is hand-maintained, not transcluded: the WG21 pandoc pipeline
in fixpoint/main/papers/wg21 (data/filters/wg21.py, driven by the Makefile's
`$(PANDOC) --bibliography ...` rule) is a formatting filter only — it
resolves cmptable layout, stable names, and coloring, with no file-include
or transclusion directive comparable to org-transclusion. Markdown has no
equivalent mechanism available in this build. The signatures below mirror,
verbatim, the following anchored regions in beman.tree_algorithms:
  - include/beman/tree_algorithms/recursion_schemes.hpp
    fold_fix (explicit-fmap primary verb): anchor 6859fdfc-3e1c-4d8a-9375-521df52ff499
    (unfold_fix/refold explicit-fmap overloads follow immediately in the
    same header; they are not individually anchored)
  - include/beman/tree_algorithms/recursion_schemes_lookup.hpp
    fold_fix (lookup overload): anchor d4f69d2f-787b-4741-ae82-971f0ff36b9e
    unfold_fix (lookup overload): anchor 2df7f7e0-ee8b-42b6-a0a8-1dfed05c8230
    refold (lookup overload): anchor c75161e3-1aad-4549-86cc-1dc40d6623c8
  - include/beman/tree_algorithms/recursion_schemes.hpp
    fold_with (direct fold): anchor 6c596985-e990-4808-a504-2b651c34cebc
    unfold_with (direct build): anchor d160af7b-9a3c-4593-b8f5-fc1ff0404b37
  - include/beman/tree_algorithms/fold_map.hpp
    fold_map over Fix (explicit): anchor 52588460-d05a-43f9-97cc-01aa06732730
    fold_map direct (explicit): anchor 5bf78776-81e7-4b66-8487-6615adc33b17
  - include/beman/tree_algorithms/fold_map_lookup.hpp
    lookup variables: anchor 94d2dd48-8976-43fc-943d-0dd27177e41a
    fold_map over Fix (lookup): anchor e4beac7f-257b-4f2d-bfff-6b3b5daf61ad
    fold_map direct (lookup): anchor 7f08b12d-3d26-4b5e-bf79-66d38f6fee0d
Checked against: manually diffed against the headers above on 2026-07-06
(three verbs) and 2026-07-14 (fold family additions); identical modulo
trailing semicolons (this is a declaration-only synopsis) and line
wrapping. Carried into D4322R0 on 2026-07-17 with no header signature
changes since the 2026-07-14 diff. Re-diff before every revision that
touches these headers, since drift here will not be caught by the build.
-->
```cpp
namespace beman::tree_algorithms {

// Recursive fold (catamorphism in the literature).
// algebra : F<Result> -> Result
// fmap_fn : (recurse, layer) -> mapped-layer
template <typename Result, template <typename> class F, typename Algebra,
          typename FMap>
constexpr auto fold_fix(const Algebra& algebra, const FMap& fmap_fn,
                        const Fix<F>& tree) -> Result;

// Recursive build (anamorphism in the literature).
// coalgebra : Seed -> F<Seed>
template <template <typename> class F, typename Coalgebra, typename FMap,
          typename Seed>
constexpr auto unfold_fix(const Coalgebra& coalgebra, const FMap& fmap_fn,
                          const Seed& seed) -> Fix<F>;

// Fused build-then-fold (hylomorphism in the literature).
// No intermediate Fix<F> is materialized.
template <typename Result, template <typename> class F, typename Algebra,
          typename Coalgebra, typename FMap, typename Seed>
constexpr auto refold(const Algebra& algebra, const Coalgebra& coalgebra,
                      const FMap& fmap_fn, const Seed& seed) -> Result;

// The direct verbs: the same folds over a tree in its own
// representation, through an explicitly supplied projection or
// embedding. Both are refold in disguise; no Fix is materialized.
// project : Tree -> F<Handle>   (one layer; handles as project accepts)
// embed   : F<Tree> -> Tree     (rebuild one layer)

template <typename Result, typename Algebra, typename FMap,
          typename Project, typename Tree>
constexpr auto fold_with(const Algebra& algebra, const FMap& fmap_fn,
                         const Project& project, const Tree& tree)
    -> Result;

template <typename Tree, typename Coalgebra, typename FMap,
          typename Embed, typename Seed>
constexpr auto unfold_with(const Coalgebra& coalgebra, const FMap& fmap_fn,
                           const Embed& embed, const Seed& seed) -> Tree;

// The elementwise fold (separate header): map each element, combine
// under an explicit associative operation with identity. The
// per-representation ingredient is layer_fold, which folds one layer's
// mapped elements with already-folded child results in the
// representation's contractual order.

template <typename Result, template <typename> class F, typename MapFn,
          typename Combine, typename LayerFold, typename FMap>
constexpr auto fold_map(const MapFn& map_fn, const Combine& combine,
                        const Result& identity, const LayerFold& layer_fold,
                        const FMap& fmap_fn, const Fix<F>& tree) -> Result;

template <typename Result, typename MapFn, typename Combine,
          typename LayerFold, typename FMap, typename Project,
          typename Tree>
constexpr auto fold_map(const MapFn& map_fn, const Combine& combine,
                        const Result& identity, const LayerFold& layer_fold,
                        const FMap& fmap_fn, const Project& project,
                        const Tree& tree) -> Result;

// Lookup-based convenience overloads (separate headers; consume the
// P3200 bundled-customization facility, resolving fmap through
// functor_typeclass<Layer>, the layer fold through
// layer_fold_typeclass<Layer>, and the projection through
// project_typeclass<Tree>).

template <typename Result, template <typename> class F, typename Algebra>
constexpr auto fold_fix(const Algebra& algebra, const Fix<F>& tree)
    -> Result;

template <template <typename> class F, typename Coalgebra, typename Seed>
constexpr auto unfold_fix(const Coalgebra& coalgebra, const Seed& seed)
    -> Fix<F>;

template <typename Result, template <typename> class F, typename Algebra,
          typename Coalgebra, typename Seed>
constexpr auto refold(const Algebra& algebra, const Coalgebra& coalgebra,
                      const Seed& seed) -> Result;

template <typename Result, template <typename> class F, typename MapFn,
          typename Combine>
constexpr auto fold_map(const MapFn& map_fn, const Combine& combine,
                        const Result& identity, const Fix<F>& tree)
    -> Result;

template <typename Result, typename MapFn, typename Combine, typename Tree>
constexpr auto fold_map(const MapFn& map_fn, const Combine& combine,
                        const Result& identity, const Tree& tree) -> Result;

} // namespace beman::tree_algorithms
```

# Folding and traversing trees

## Elementwise folds: fold_map

The structural verbs consume a whole layer at a time; many everyday
queries see only elements.
Length, sums, to-string renderings, any/all queries — each maps every
element to a result type and combines the results under an associative
operation with an identity.
`fold_map` is that verb.

The obvious question is why `fold_map` when `std::ranges::fold_left`
exists.
The answer is this paper's whole subject: `fold_left` folds ranges, and
recursive trees are not ranges.
Flattening a tree into an iteration order to fold it discards the
structure, costs an intermediate sequence, and forecloses folds that
exploit the shape.
`fold_map` folds the tree where it stands — and because the combine
operation is required to be associative with a two-sided identity, the
result is independent of how the structure happens to be balanced.
The reference implementation demonstrates exactly that: the same element
sequence held in two differently shaped trees folds to the same result,
while a reordered sequence folds to a different one.

The per-representation ingredient is `layer_fold`: the callable that
folds one layer, combining that layer's mapped elements with the
already-folded child results in the representation's contractual
traversal order.
Traversal order is part of each instance's contract — in-order for the
value-at-every-node binary tree, sequence order for the fringe tree,
pre-order for the rose tree — and the test policy pins it with
non-commutative combines (string concatenation), under which a wrong
order is a wrong answer.

Order belongs to the instance because no generic order exists to impose.
A rose tree — a value and any number of children — has no "between the
children" position, so in-order traversal does not exist for that shape;
in-order is the binary layer's contract, not a property of trees.
The reference implementation pins the consequence in a test: the same
`fold_map` call over the same three values yields `"123"` through the
binary layer and `"213"` through the rose layer, and neither is wrong,
because there is no generic in-order to be right about.
Pre-order and post-order do generalize — every layer has a
before-children and an after-children moment — so a future generic
traversal surface could offer those.
Never in-order.

The derivation is the implementation, and that is the point: `fold_map`
over a fixed-point tree *is* `fold_fix` with an algebra that combines
one layer through the supplied operations, and `fold_map` over a
projected tree *is* `fold_with` with the same algebra.
Elementwise folding is derived from the structural verbs, not parallel
machinery — evidence that the small proposed core is the right primitive
layer.
The derivations continue past this paper's surface: the classic
Foldable operations (`fold_left` and its family) derive from `fold_map`
by folding elements into accumulator-transforming programs, exactly as
Haskell's `Data.Foldable` derives `foldl` from `foldMap`; the reference
implementation demonstrates the derivation, and the coordinated set
keeps those derived operations with the Foldable surface rather than
respecifying them here.

What counts as an element is the instance's choice, not the framework's.
The expression tree's elements are its constants: summing the constants
of `(1 + 2) * 3` by `fold_map` gives `6`, while evaluating it by
`fold_fix` gives `9`.
One fixture, both verbs, and the elementwise/structural distinction is
visible in the values.

## Traversal with effects: the P3200 boundary

A companion facility, P3200's `traverse`/`transpose`, covers the
remaining member of this family: rebuilding the *same shape* while
per-element effects accumulate in a context — validating every constant
in an expression tree into `optional<Expr>`, where one absent value
poisons the whole result but a present result preserves the exact
structure.
Where `fold_map` collapses a tree to a summary, `traverse` maps a tree
to a tree inside a context; `transpose` is `traverse` with the identity
function.

That facility is P3200's public identity, and this paper deliberately
does not respecify any of it.
Trees are one instance of P3200's "structure" axis, exactly as
`optional`, senders, and SIMD lanes are instances of its "context" axis;
the tree instances demonstrated in the reference repositories are
examples, not wording.
Like `fold_map`, `traverse` for any fixed-point tree is derivable
through the layer's mapping function — one more derived operation over
the same primitive layer, on the other side of the papers' division of
labor.

# Naming

The public identifiers are `fold_fix`, `unfold_fix`, `refold`, `fold_with`, `unfold_with`, and `fold_map`.
"Recursive fold" and "recursive build" are prose descriptions used in this paper; they are not identifiers.

The traditional names for these three schemes — catamorphism, anamorphism, hylomorphism — are precise, but they are jargon that most C++ programmers have no reason to know.
The Haskell `data-fix` library (0.3.4) reached the same conclusion and deprecated `cata`, `ana`, and `hylo` in favor of descriptive names; this proposal follows that lead.

| Traditional | This proposal | `data-fix`  |
|-------------|---------------|-------------|
| cata        | `fold_fix`    | `foldFix`   |
| ana         | `unfold_fix`  | `unfoldFix` |
| hylo        | `refold`      | `refold`    |

Alternatives considered and rejected:

| Candidate                          | Why rejected                                                                                                   |
|------------------------------------|----------------------------------------------------------------------------------------------------------------|
| `cata` / `ana` / `hylo`            | Jargon with no descriptive content for a C++ audience; `data-fix` precedent is to deprecate these in favor of descriptive names; not proposed even as aliases. |
| `catamorphism` / `anamorphism` / `hylomorphism` | Same jargon problem, longer.                                                                     |
| `recursive_fold` / `recursive_build` | Verbose as identifiers, and collides with the prose descriptions this paper uses to *explain* the verbs; the `_fix` suffix instead names the fixed-point mechanism the verbs operate through, matching `data-fix`. |
| `fold` / `unfold` alone            | Overclaims generality and collides with existing fold vocabulary (`std::ranges::fold_left` and friends) that is sequence-shaped, not structure-shaped. |

The reference evidence repository carries a `[[deprecated]] cata` alias for its own history; this proposal starts clean and does not include it.

The direct verbs are `fold_with` and `unfold_with`: fold and build *with* an explicitly supplied ingredient — the projection or embedding one would otherwise have to bake into a conversion.
The suffix reads consistently across the coordinated set, where `_with` marks "the thing you would otherwise look up, passed inline" (P3200 spells explicit-instance overloads `traverse_with`/`transpose_with` in the same spirit).

| Candidate                      | Why rejected                                                                                                            |
|--------------------------------|-------------------------------------------------------------------------------------------------------------------------|
| `fold_tree` / `unfold_tree`    | Names the subject rather than the distinguishing ingredient; the verbs are not tree-specific, only recursive-structure-specific. |
| `fold_from` / `unfold_into`    | Directional pair reads well for build, poorly for fold; asymmetric.                                                     |
| `recursive_fold` / `recursive_build` | Rejected for the structural verbs above; the same reasoning applies.                                              |
| `*_fix` spellings              | The `_fix` suffix names the fixed-point mechanism; the direct verbs pointedly never touch `Fix`, so borrowing it would misstate the design. |

`fold_map` keeps the cross-language consensus name (`foldMap` in Haskell's `Data.Foldable`, Scala's Cats, and PureScript): it says exactly what the verb does — map, then fold — and its contrast with `fold_fix` (elements versus layers) is the teaching device of the section that introduces it.

# Implementation experience

Two implementations back this paper.

**`beman.tree_algorithms`** is the standardization-facing implementation: exactly the surface proposed here — `Fix`/`Box`, the six verbs in both their explicit and lookup tiers, the three consumer-stub lookup objects, and adapters demonstrating the verbs over five recursive representations:

- the fixed-point expression tree (`Fix` over a variant base functor), plus a full working binary search tree built on the same machinery (see [Building on Fix directly](#building-on-fix-directly));
- a persistent value-at-every-node binary tree with `shared_ptr` children, served both by conversion (`to_fix`/`from_fix`) and directly (a projection reading the `shared_ptr` spine in place), with a test pinning that both routes fold the same tree to the same answer;
- a rose (multiway) tree, `Fix`-native: one base functor holding a value and a `std::vector` of children — no `Box` at all, because `vector` supports incomplete element types and already owns through indirection — whose layer fold contracts pre-order, the shape for which in-order does not exist;
- a fringe tree — values at leaves, branches carrying a cached measure (leaf count), O(1) concatenation by structural sharing — served *directly only*: its layer holds children by value, no `Fix` form exists for it, and its embedding routes through the measure-computing constructor, so every tree `unfold_with` builds carries correct cached measures by construction;
- a nonce tree with `std::unique_ptr` children — move-only, uncopyable, folded and built in place through three small ingredients.

It targets C++23, with tests in Catch2, including compile-time (`static_assert`) coverage for every verb, and equivalence laws pinning each derived form to the primitives: `fold_with` with `unwrap_fix` as its projection is `fold_fix`; `unfold_with` with `wrap_fix` as its embedding is `unfold_fix`; `fold_map` equals `fold_fix` with the monoid algebra written by hand.
Its test policy deliberately uses non-idempotent, order-sensitive algebras, because experience showed that idempotent or commutative-monoid-only test functions fail to distinguish a correct traversal from a plausibly wrong one.
The policy caught a real defect during this paper's own development: a draft `fold_map` test used `combine(a, b) = a*10 + b` as its "non-commutative combine" — an operation that is neither associative nor equipped with a two-sided identity, and the leaf positions, which combine against the identity, exposed it immediately in a failing `static_assert`.
The canonical lawful non-commutative monoid — strings under concatenation — is what the suite uses, at compile time as well as runtime.

**The `smd::fixpoint` evidence repository** implements, on top of the same three-verb core, the full catalog of recursion schemes from the literature: para, apo, zygo, mutu, prepro, postpro, histo, futu, dyna, codyna, chrono, Mendler-style mcata/mhisto, Elgot (co)algebras, distributive laws, and the generalized gcata/gana/ghylo family, each gated by equivalence-law tests pinning it to the core verbs (for example `gcata(dist_cata) ≡ fold_fix`).
That catalog is deliberately **not** proposed here.
Its role is evidence: the three proposed verbs are the stable primitive layer of a much larger, independently well-studied algorithm family, and nothing in the extended catalog required revisiting their design.

## Costs

We are upfront about the costs of this design; they are better stated by the authors than discovered by reviewers.
The library chooses value semantics throughout — layers are passed by value, `Box` deep-copies, and there is no sharing or memoization — and the abstraction itself has two costs beyond the copies.
First, compile time: C++ lacks higher-kinded types, so the `fmap` that `fold_fix` requires is emulated by template machinery, and the compiler performs deep, recursive template instantiation to type-check each scheme at each carrier type.
Across a large codebase this instantiation overhead can measurably degrade build latency.
<!-- MEASURE: no compile-time numbers exist for beman.tree_algorithms; the claim is qualitative, from the source cost analysis. Re-measure (e.g. -ftime-trace) before publication. -->

Second, code generation: a hand-written `std::visit` traversal compiles to a jump table or switch with O(1) dispatch, whereas a recursion scheme relies entirely on the optimizer to inline the generic traversal, the algebra, and the `Box` unwrapping into an equivalent loop.
When the structure's nesting exceeds inlining thresholds, the result is larger object code and runtime pointer-chasing rather than a fused traversal.
<!-- MEASURE: codegen size and per-node cost versus a manual std::visit walker have not been re-measured for this implementation. -->
These costs amortize well in one common deployment: the traversal machinery is stable and lives in rarely-recompiled headers, while the frequently-edited code consists of small, non-recursive algebras that compile like ordinary function objects.

We also note the language-evolution horizon.
Core-language pattern matching (P1371 "Pattern Matching"; superseded by P2688 "Pattern Matching: match Expression", still under EWG review after missing C++26) would replace the per-node dispatch inside an algebra, not the recursive traversal itself.
The proposed facility degrades gracefully into that future: algebras get shorter under a `match` expression, and `fold_fix` continues to supply the fold that the core language will still not provide.

# Non-goals

- **Standardizing one canonical tree vocabulary type.**
  This is an algorithms proposal; no blessed tree layout or preferred recursive representation strategy is proposed.
- **A `Recursive`/`Corecursive` concept for projections and embeddings.**
  The direct verbs take `project` and `embed` as plain callables; no concept constrains them and no typeclass registers them beyond the optional lookup convenience.
  Plain callables compose unchanged with the generalized schemes of the evidence catalog; a concept can be layered on later without disturbing anything proposed here.
- **An elementwise traversal (`traverse`/`transpose`).**
  Shape-preserving effectful traversal is P3200's public identity; this paper's tree instances of it are examples, never wording (see [Folding and traversing trees](#folding-and-traversing-trees)).
- **The exotic recursion-scheme catalog.**
  para, apo, zygo, histo, futu, the distributive laws, and the generalized family remain implementation evidence, out of scope for this paper.
- **Performance-tuned traversal.**
  The proposed semantics are value semantics: layers are held and passed by value, copies are accepted, and there is no sharing, no move-optimization pass, and no memoization.
  Performance work is an explicit non-goal for this revision.
- **Stack-safety guarantees.**
  The algorithms are recursive; deep structures can overflow the stack, as the naive functional formulations can.
- **General graph algorithms.**
  Recursive trees only; graphs are a different proposal space.
- **Dependence on the persistent-sequence paper.**
  Nothing here requires prior acceptance of Paper C.

# Relationship to companion papers

This paper is part of a coordinated set, each paper independently reviewable.

**What this paper covers on its own**: the six verbs with their explicit primary APIs — explicit `fmap`, explicit projection/embedding, explicit combine and identity — usable with no other paper adopted.

**P3200 (Paper A: traversal, transposition, and bundled customization)**: proposes the bundled-customization mechanism (typeclass-object lookup) and the shape-preserving traversal/transposition story over familiar vocabulary types.
This paper *consumes* that mechanism in its convenience overloads — `fold_fix(algebra, tree)` resolving `fmap` through `functor_typeclass`, `fold_map(map_fn, combine, identity, tree)` resolving its structural ingredients the same way — and never respecifies it.
The mechanism debate belongs in P3200; the division of labor is deliberate.
The elementwise fold proposed here and the effectful traversal proposed there are two halves of one family, split on motivation: `fold_map`'s case is structures that are not ranges, which is this paper's subject; `traverse`'s case is contexts (`optional`, senders, SIMD lanes), which is P3200's.

**Paper C (persistent measured sequence)**: an independently motivated container proposal.
There is no normative dependence in either direction; persistent tree structures are simply one more recursive representation the algorithms here can serve.
The fringe tree in this paper's reference implementation — a cached measure maintained through its embedding — is the miniature of Paper C's monoid-tagged-structure idea, and the shared `Monoid` vocabulary (`identity`/`combine`) is kept aligned across the coordinated set.
The Foldable derivations sit on the same boundary: `fold_map` is the primitive this paper proposes, and the operations that derive from it (`fold_left` and its family) live with the Foldable surfaces in the companion repositories — one definition each, derived, never respecified here.

**Monad, deferred**: no paper in the coordinated set proposes a Monad abstraction — deferred, not rejected.
The typeclass design points toward a consistent generic name for sequential composition eventually (today's `and_then`/`transform`/`let_value` are per-type members, not generic), and the mechanism demonstrably carries it; it is simply not needed by anything proposed — the verbs here fold and build, and the traversal paper composes independent contexts.
The rationale is stated once, citably, in P3200's "Why not Monad"; this paper records only that the omission is a decision.

**What remains valid if companion papers do not progress**: the entire primary API.
Only the lookup-based convenience overloads are contingent on P3200's facility.

# References

- Steve Downey, `beman.tree_algorithms` reference implementation. <https://github.com/steve-downey/tree_algorithms>
- Steve Downey, `smd::fixpoint` evidence repository (full recursion-scheme catalog) and *Fixpoint Trees in C++*.
- P3200, the companion traversal/transposition and bundled-customization paper (Paper A of this coordinated set).
- Haskell `data-fix` 0.3.4, `Data.Fix`: `foldFix`/`unfoldFix`/`refold` naming precedent. <https://hackage.haskell.org/package/data-fix-0.3.4/docs/Data-Fix.html>
- Haskell `base`, `Data.Foldable`: the `foldMap` naming and derivation precedent for `fold_map`. <https://hackage.haskell.org/package/base/docs/Data-Foldable.html>
- Haskell `containers`, `Data.Tree`: the rose-tree precedent. <https://hackage.haskell.org/package/containers/docs/Data-Tree.html>
- Edward Kmett, `recursion-schemes` (Haskell package): the extended catalog implemented as evidence. <https://hackage.haskell.org/package/recursion-schemes>
- Erik Meijer, Maarten Fokkinga, Ross Paterson, *Functional Programming with Bananas, Lenses, Envelopes and Barbed Wire*, FPCA 1991.
- Steve Downey, *Concept Maps Using C++23 Library Tech*. <https://sdowney.org/posts/index.php/2024/05/19/concept-maps-using-c23-library-tech/>
- P2988, `std::optional<T&>`; and `std::indirect` (P3019), the plausible future spelling of the `Box` exposition detail.
