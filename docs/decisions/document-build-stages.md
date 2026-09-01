# build_document decomposes into classify, build, group

## Status

Accepted

## Context

A single loop doing four jobs at once — classifying AST items, maintaining the
`\rSec` frame stack, attaching items, and applying `\also`/`\omit` grouping by
mutating `children.back()` mid-loop (with a comment explaining why a pointer
would dangle) — deepens with every new directive and cannot be tested in
pieces.

## Decision

`build_document` is a pipeline of separately testable stages:

1. `classify(SourceItem) -> DocEvent` — pure per-item classification. The
   `dyn_cast` if/else chain is inherently front-end code and stays, but is
   quarantined inside this one function returning a closed variant
   (`SectionOpen{depth, …}`, `SynopsisDecl`, `ItemDecl`, `Comment`, …).
2. `build_tree(span<DocEvent>) -> ir::Document` — the `\rSec` frame stack,
   isolated. "Parse a flat sequence into a tree by depth" is kept as **one**
   marked substrate algorithm (a fold carrying a frame stack) rather than
   forced into `unfold_with` — the events arrive linearly with explicit
   depths, and a stack-fold states that directly; an unfold would need
   lookahead plumbing for no gain. Testable without Clang using synthetic
   events.
3. `group_items(ir::Document) -> ir::Document` — `\also`/`\omit` grouping as a
   post-pass. Shape: a per-`Section` transform of `vector<Node>` using
   `views::chunk_by` on the "attaches to previous primary" relation, then a
   per-chunk merge. Pure core code once directives ride the tree;
   unit-testable without Clang.

## Consequences

- Directive work (`\describe`/`\at`/`\merge`, `\omit` synopsis exclusion)
  lands as new stage-3 cases rather than deepening a monolithic loop.
- The Clang dependency is confined to stage 1; stages 2 and 3 are testable at
  the library level with synthetic inputs.
- The mid-loop `children.back()` mutation and its dangling-pointer hazard are
  gone by construction: grouping operates on a completed tree.
