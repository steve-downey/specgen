# Engineering Reality — publication-ready drafts (plan Task 4.3)

Distilled from `fixpoint/main/src/smd/fixpoint/replacing_vist.md` lines 52–59 ("The Engineering Reality of the Abstraction"), per `docs/notes/article-reconciliation-memo.md` §4 and `docs/DECISIONS.md` Decisions 2 and 6.
Lines 1–49 of the source essay are discarded as a stale duplicate; its code excerpts (deprecated `cata`, wrong call shapes) are not authoritative (DECISIONS.md, "Decision 4 citation" notes, lines 129–131).

Required edits applied:

- "30,000-package build graph" claim softened to an unquantified large-codebase statement (memo §4).
- `cata` replaced with `fold_fix` throughout (Decision 2).
- Pattern matching cited by paper number, P2688 and P1371, with current status (memo §4).
- Claims needing fresh numbers against `beman.tree_algorithms` are flagged `# MEASURE:` (Draft A) or `<!-- MEASURE -->` (Draft B).

---

## Draft A — article section (org-mode)

```org
* The Cost of the Abstraction

Everything above isolates recursion from business logic: you write the shape of one layer and the evaluation of one node, and ~fold_fix~ supplies the traversal.
That separation is real, and so are its costs.
This library deliberately chooses value semantics --- layers are held and passed by value, ~Box~ deep-copies, and no sharing or memoization pass tries to claw the copies back --- so it is worth being explicit about where the bill arrives: at compile time, and in what the optimizer must do for you at run time.

The first cost is compile-time latency.
C++ has no true higher-kinded types, so the ~fmap~ that ~fold_fix~ depends on is emulated with template machinery: the compiler must instantiate the base functor at each carrier type, prove each instantiation well-formed, and do so recursively as the scheme threads results back up through the structure.
Each individual instantiation is cheap; the concern is the aggregate.
Across a large codebase --- many translation units, orchestrated by a build system coordinating a large dependency graph --- the overhead of instantiating these deeply nested template structures can measurably degrade build latency.
# MEASURE: quantify instantiation counts and wall-clock compile cost for a representative ExprF/fold_fix TU against beman.tree_algorithms (e.g. -ftime-trace); the source essay asserts the tax but reports no numbers.

The second cost is code generation.
A hand-written ~std::visit~ over a recursive ~std::variant~ compiles down to a highly optimized jump table or switch --- O(1) dispatch per node, with the traversal loop visible to the optimizer as ordinary code.
A recursion scheme instead hands the optimizer a stack of generic components --- the scheme, the ~fmap~, the algebra, and the ~Box~ unwrapping between layers --- and relies entirely on inlining to fuse them back into that same loop.
When inlining succeeds, the abstraction can be free.
When the nesting pushes past the compiler's inlining thresholds, it is not: the residue is bloated object code and runtime pointer-chasing through the indirections that ~Box~ introduces.
# MEASURE: compare generated code size and per-node evaluation cost of fold_fix versus a hand-rolled std::visit walker on the same ExprF, at -O2, for beman.tree_algorithms; the jump-table-versus-inlining contrast comes from the source essay and has not been re-measured here.

So when is it worth paying?
The costs above are concentrated in the traversal machinery --- ~Fix~, ~Box~, ~fmap~, and the schemes --- and that machinery is precisely the part of the code that does not change.
The algebras, which carry the business logic, are small, non-recursive, and cheap to compile.
The amortization strategy follows directly: keep the machinery in stable, rarely-recompiled headers behind a settled interface, and let the frequently-edited code consist of algebras.
Then the template-instantiation tax is paid when the machinery changes --- rarely --- while the everyday edit-compile loop touches only code that compiles like any other small function object.
A codebase that edits its tree-walking logic often and its tree shapes rarely gets the good end of this trade; one that constantly invents new recursive shapes pays the tax repeatedly.

Finally, the horizon.
The visitation half of this story --- destructuring a sum type without ~std::visit~ boilerplate --- is also being pursued in the core language, in the pattern-matching lineage of P1371 ("Pattern Matching") and its successor P2688 ("Pattern Matching: match Expression").
As of this writing neither has landed: pattern matching did not make C++26, and P2688 remains under Evolution Working Group review targeting a later standard.
When a ~match~ expression does arrive, it will replace the per-node dispatch --- the ~std::get_if~ chains inside an algebra --- but not the traversal: something must still fold results up through the recursive structure.
A library design should degrade gracefully into that world, and this one does: algebras written against the base functor become shorter and clearer under language pattern matching, while ~fold_fix~ keeps supplying exactly the part the language will still not provide.
Until then, the recursion schemes eliminate visitor boilerplate by library means alone --- provided the build-time costs above can be amortized as described.
```

---

## Draft B — D-paper subsection (WG21 markdown)

```markdown
### Costs

We are upfront about the costs of this design; they are better stated by the authors than discovered by reviewers.
The library chooses value semantics throughout — layers are passed by value, `Box` deep-copies, and there is no sharing or memoization — and the abstraction itself has two costs beyond the copies.
First, compile time: C++ lacks higher-kinded types, so the `fmap` that `fold_fix` requires is emulated by template machinery, and the compiler performs deep, recursive template instantiation to type-check each scheme at each carrier type.
Across a large codebase this instantiation overhead can measurably degrade build latency.
<!-- MEASURE: no compile-time numbers exist for beman.tree_algorithms; the claim is qualitative, from the source cost analysis. Re-measure (e.g. -ftime-trace) before P-numbering. -->

Second, code generation: a hand-written `std::visit` traversal compiles to a jump table or switch with O(1) dispatch, whereas a recursion scheme relies entirely on the optimizer to inline the generic traversal, the algebra, and the `Box` unwrapping into an equivalent loop.
When the structure's nesting exceeds inlining thresholds, the result is larger object code and runtime pointer-chasing rather than a fused traversal.
<!-- MEASURE: codegen size and per-node cost versus a manual std::visit walker have not been re-measured for this implementation. -->
These costs amortize well in one common deployment: the traversal machinery is stable and lives in rarely-recompiled headers, while the frequently-edited code consists of small, non-recursive algebras that compile like ordinary function objects.

We also note the language-evolution horizon.
Core-language pattern matching (P1371 "Pattern Matching"; superseded by P2688 "Pattern Matching: match Expression", still under EWG review after missing C++26) would replace the per-node dispatch inside an algebra, not the recursive traversal itself.
The proposed facility degrades gracefully into that future: algebras get shorter under a `match` expression, and `fold_fix` continues to supply the fold that the core language will still not provide.
```
