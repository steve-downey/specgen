# ir::Node stays as-is; a base functor NodeF with fold_with projection

## Status

Accepted

## Context

The `ir.hpp` types are the serialization contract, and code throughout the tool
builds against them, so they cannot change shape. But every operation over the
IR tree — JSON emission, each backend, each validator — is a recursion over the
same structure, and hand-written recursive function sets multiply.

## Decision

Keep the `ir.hpp` types byte-identical. Add, in a separate header
`ir_fold.hpp`, the rose-tree treatment from `tree_algorithms`:

- `NodeF<A> = std::variant<SectionF<A>, Synopsis, SpecItem, FreeParagraph>`
  with `SectionF<A>{stable_name, title, std::vector<A> children}` — three
  alternatives are leaves; only `SectionF` carries the recursion slot, and
  `std::vector` supplies the indirection, so no `Box`, no `child_slot_t`, no
  `std::indirect` needed here.
- `node_project(const Node&) -> NodeF<std::reference_wrapper<const Node>>`,
  `node_embed(NodeF<Node>&&) -> Node`, and
  `node_fmap(f, NodeF<A>) -> NodeF<B>` (maps only `SectionF::children`; leaf
  alternatives pass through).
- Consumers call the vendored
  `fold_with<Result>(algebra, node_fmap, node_project, node)` /
  `unfold_with(coalgebra, node_fmap, node_embed, seed)`. `ir::Document` is a
  forest: fold each root, combine.

## Consequences

- `fold_with` is the verb for "fold a tree in its own representation": it
  works on `const Node&` directly, without materializing a `Fix` or copying
  the IR.
- `emit_json(Node)`, every backend, and every validator become algebras
  `NodeF<Result> -> Result`; a new operation over the IR is a new algebra, not
  another recursive function set.
- The columnar `tagged_tree` alternative is rejected: its advantages —
  non-recursive linear folds, no stack limits, `scan_down` for inherited
  attributes — require a representation change that would break the JSON
  contract, to solve a recursion-depth problem specgen does not have (wording
  documents nest `\rSec` perhaps five deep, node counts in the hundreds). A
  `Fix`-materialized tree is also rejected: nothing needs to own a generic
  fixpoint; the IR types are already the canonical owned representation.
