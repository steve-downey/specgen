# Reconciliation Memo: "Fixpoint Trees in C++" (trees copy vs fixpoint copy)

Input for the new-article drafting task (plan Task 4.1).
Copy A = `trees/main/docs/fixpoint-trees-in-cpp.org`; Copy B = `fixpoint/main/src/smd/fixpoint/fixpoint-trees-in-cpp.org`.

## Headline finding

**The two copies are byte-identical** (both 822 lines; empty diff).
B is a verbatim import into the fixpoint repo; A is the original.
The real divergence is **article-vs-code**: the text was written against the trees repo, so A's citations all resolve while B's mostly dangle, and the fixpoint repo has evolved the code (hand-rolled `Box`, `constexpr` schemes, functor-lookup overloads) past what the article describes.
"Canonical" below means "which repo's *code* the section matches."

## 1. Section-by-section verdicts

| Section (lines) | Difference vs current code | Verdict |
|---|---|---|
| Building an Expression Tree in 20 Lines (32–113) | Uses `Box = std::indirect<A>`; "any C++23 toolchain" claim only true with hand-rolled Box | PORT, re-anchor Box |
| The Pieces intro (115–119) | Names trees-repo file/constructors (`ExprConst/ExprAdd/ExprMul`, `double`); fixpoint has `Const/Add/Mul`, `int` | MERGE/REWRITE for new repo names |
| The Non-Recursive Functor (121–154) | none | PORT, retarget file names |
| Box: Breaking Infinite Instantiation (156–170) | Article says `std::indirect` + P3019; shipping Box is hand-rolled constexpr with rationale comment | PORT + REWRITE |
| Fix: Tying the Recursive Knot (172–207) | none | PORT as-is |
| Composition, not inheritance (209–226) | none | PORT verbatim (from A — resolvable Niebler citation) |
| fmap: Lifting a Function Over One Layer (228–262) | Article shows free-function `fmap_expr`; fixpoint dispatches via `functor_typeclass<ExprF<A>>` + `layer_fmap` | MERGE: keep pedagogy, swap in lookup version |
| Smart Constructors (263–295) | name/type drift only | PORT, rename |
| What Is an F-Algebra? (299–325) | none | PORT, re-anchor `eval_algebra` |
| fold_fix + How it unfolds (327–394) | Article omits `constexpr` and the lookup-based 2-arg overloads | PORT + EXTEND with lookup overload |
| A Different Algebra, A Different Result (396–438) | trees-only citation | PORT, re-anchor |
| unfold_fix (440–483), refold (485–522) | none | PORT, use lookup overloads; keep fold∘unfold ≡ refold law |
| A Note on Names (524–537) | none; verify new repo does NOT ship deprecated `cata` before keeping line 536 | PORT verbatim, drop stale alias sentence |
| Foldable sections (539–649) | trees-only anchors | LEAVE, except lift typeclass-object exposition (544–556) into functor-lookup section retargeted to `functor_typeclass` |
| Traversable sections (651–796) | trees-only | LEAVE (out of minimal-core scope) |
| The Bigger Picture (798–821) | cites fringe/finger trees | MERGE: keep recap + BinaryTree; cut/soften Fringe/Finger; drop fold_map/traverse recap lines |

No exotic-scheme material exists in either copy; nothing to LEAVE on that ground.

## 2. Key passages

- **"Composition, not inheritance"**: lines 209–226, identical in both. First line: "Niebler [cite:@niebler2013falgebras] translates this pattern to C++ using inheritance:".
- **"A Note on Names"**: lines 524–537, identical in both. First line: "The traditional names for these three schemes --- catamorphism, anamorphism, hylomorphism --- are precise, but they are jargon...".
- Use Copy A as source: only the trees repo has `docs/references.bib` resolving the citations.

## 3. Re-anchoring requirements

Copy B dangles 10 of 15 cited files (all `src/smd/tree/*` and typeclass adapter files).
Copy A resolves all 15 but every `orgit-file:` link and both `std::indirect` code blocks (lines 40–65, 164–170) must be re-anchored to `beman::tree_algorithms` paths and the hand-rolled Box.
Bib keys needed in the new repo: `milewski2013falgebras`, `milewski2017falgebras`, `niebler2013falgebras`, `gibbons2006iterator`, `mcbride2008applicative` (all in `trees/main/docs/references.bib`).

## 4. Engineering Reality insertion

`fixpoint/main/src/smd/fixpoint/replacing_vist.md` lines 1–49 are a weaker duplicate of the article's own recursion-schemes exposition with stale anchors — **discard**.
Lines 52–59, "The Engineering Reality of the Abstraction," are the unique portable content: compile-time latency from HKT emulation, code-bloat-vs-inlining versus `std::visit`'s O(1) jump table, pattern-matching outlook.
Neither org copy has a costs/limitations section, so no duplication risk.
Insert as a new section **immediately after "A Note on Names"**, closing the recursion-schemes chapter.
Required edits: soften the unanchored "30,000-package build graph" claim; replace `cata` with `fold_fix`; cite pattern matching by paper number (P2688/P1371) and current status.

## 5. Porting punch list (ordered)

1. Opening section: port; replace `std::indirect` alias block with hand-rolled `Box`; the "any C++23 toolchain" Godbolt claim becomes true with that change.
2. The Pieces intro: rewrite file path and constructor names to the new repo's actuals (decide `ExprConst/double` vs `Const/int`; make code and prose agree).
3. Non-Recursive Functor: port; keep the three F-algebra citations; copy bib entries.
4. Box: port heading + first two sentences; replace `std::indirect` claim with hand-rolled Box + rationale from `box.hpp` lines 10–19; keep P3019 as "why not std::indirect (yet)".
5. Fix + wrap_fix/unwrap_fix: port verbatim.
6. Composition, not inheritance: port verbatim.
7. fmap: merge — keep shape-knowledge pedagogy paragraph (259–261); present `functor_typeclass<ExprF<A>>` instance + lookup as the mechanism.
8. Smart constructors: port; rename.
9. F-algebra: port; re-anchor `eval_algebra`.
10. fold_fix + walkthrough: port verbatim; lookup-based 2-arg `fold_fix` as user-facing form, explicit-fmap 3-arg as escape hatch; note constexpr.
11. Different algebra: port; re-anchor print example to a new-repo test.
12. unfold_fix, refold: port; lookup overloads; keep the law + "test suite checks this".
13. A Note on Names: port verbatim minus the deprecated-alias sentence (new repo ships no aliases, per DECISIONS.md Decision 2).
14. NEW Engineering Reality section per §4.
15. Typeclass Object Pattern exposition (544–556): lift into functor-lookup section, retargeted to `functor_typeclass`.
16. Binary tree adapter section: **write fresh** — no source section exists in either copy (gap, not a port).
17. Closing: keep "same lookup, many trees" argument + recap stanza minus fold_map/traverse lines; keep BinaryTree; cut/soften FringeTree/FingerTree.
18. LEAVE entirely: fold_map impl (572–616), What You Get for Free (618–649), all Traversable (651–796) — earmark for a follow-up article; `replacing_vist.md` lines 1–49 — discard.
