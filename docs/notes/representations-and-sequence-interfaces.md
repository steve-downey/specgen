# Fix-native trees, representation-generic interfaces, and the sequence quotient

Date: 2026-07-17. Direction from Steve, 2026-07-16, recorded here before
the code: two asks that sound contradictory and are not.

1. Show a **classic binary tree built on Fix itself** — not an adapter
   target, a full working tree implemented with the machinery.
2. Ensure the **algorithms and interfaces are not coupled to Fix** (nor
   to Free/Cofree if those arrive): they must adapt to the naive
   `struct Node { data, left, right }`, and the *sequence-facing*
   algorithms — view left, view right, append, concat, plus the foldable
   and traversable interfaces — should generalize the way FingerTree's
   do, on the assumption (to be demonstrated concretely) that
   rebalancing can be internalized to the type and **quotient
   equivalence is all the interface requires**. Fix and Box also admit
   rose trees — with the consequence that **generic in-order is
   impossible**, since a rose tree has no in-order.

## 1. Why the asks are complementary, not contradictory

The repo's architecture already contains the reconciliation; this note
names it so the paper can.

**Fix is a representation, not the interface.** The verbs' entire
coupling surface is the layer functor plus one boundary callable:
`project : Tree -> F<Handle>` for folds, `embed : F<Tree> -> Tree` for
builds. `fold_fix` is the degenerate case where `project` is
`unwrap_fix` (extraction-plan §5; `fold_with ∘ unwrap_fix ≡ fold_fix` is
a tested law). So "an example built ON Fix" and "algorithms not coupled
TO Fix" are the same claim exercised from both ends:

- The naive `Node{data, left, right}` never materializes a Fix
  (`examples/nonce_tree_direct.cpp`, already shipped).
- Fix<F> is itself one more representation a user may build on
  directly — and that demonstration was missing until now.

The representation matrix, after this round:

| Representation | Relation to Fix | Verbs reach it via | Demonstrates |
|---|---|---|---|
| `ExprF` / `Fix<ExprF>` | Fix-based | `unwrap_fix` | the verbs themselves |
| `BinaryTree<T>` (shared_ptr) | converted (`to_fix`/`from_fix`) or direct | conversion AND `project_typeclass` | adapting a tree you don't own |
| nonce `Node` (unique_ptr) | none — no Fix ever | explicit `project` | three ingredients, zero machinery adoption |
| `FringeTree<T>` | none — no Fix, no Box | `project`/`embed` instances | measures; the sequence quotient |
| **`RoseTreeFix<T>` (new)** | **Fix-native, no separate class** | `unwrap_fix` | vector children; no Box needed; no in-order |
| **BST on `Fix<BinaryTreeF>` (new, example)** | Fix-native working container | `unwrap_fix` + plain recursion | ask 1 |

## 2. What was added (2026-07-17)

**`examples/search_tree_on_fix.cpp`** — ask 1. A full working binary
search tree on `BinaryTreeFix<int>`: a whole tree is
`Box<BinaryTreeFix<int>>`, so "possibly-empty tree" and "child slot" are
the same type. The honest division of labor is the example's thesis:
`unfold_fix` builds (balanced, from sorted), `fold_fix`/`fold_map`
consume whole trees (flatten, height, shape), while insert / contains /
erase are plain structural recursion over `wrap_fix`/`unwrap_fix` —
search *prunes*, folds visit everything, and Fix does not take ordinary
recursion away. (The pruning/short-circuit schemes — para, apo — exist
in the fixpoint catalog; deliberately not shipped here, Decision 1.)
The whole lifecycle is `static_assert`-ed at compile time. The closing
check ties to §3: insertion-order and balanced builds differ in shape,
agree in flatten.

**`include/beman/tree_algorithms/rose_tree.hpp` + tests** — the rose
tree, Fix-native: `RoseF<T, A> = { T value; std::vector<A> children }`.
No Box: `std::vector` supports incomplete element types and already owns
through indirection, so the knot ties with no extra plumbing (Box would
return for fixed-arity aggregate children — the choice is per-layer).
Functor instance and `layer_fold_typeclass` instance follow the shipped
pattern; no `project_typeclass` registration, since the Fix overloads
already unwrap it.

**The no-generic-in-order consequence, made testable.** The rose layer
has no "between the children" slot, so in-order does not exist for it;
in-order is the *binary* layer's contract. This is why `fold_map`
delegates order to the per-representation layer fold instead of imposing
a generic order: the generic thing is the fold; the order is the
instance's (DECISIONS.md Amendment 2026-07-14; coherence-skill hazard
"traversal order is contractual"). Pinned in
`rose_tree.test.cpp` ("InOrderIsBinaryNotGeneric"): the same `fold_map`
call over the same three values yields `"123"` through the binary layer
and `"213"` through the rose layer. Neither is wrong; there is no
generic in-order to be right about. Consequence for any future generic
traversal surface: pre-order and post-order generalize (every layer has
a before-children and an after-children moment); in-order does not.

**Views and the sequence quotient** —
`view_l`/`view_r` free functions on `FringeTree` (in
`fringe_tree.hpp`, below the anchored port so the ported class stays
byte-identical), returning `std::optional<FringeView<T>>` with
`d_value`/`d_rest` fields; names and shape mirror Paper C's
`FingerTree::view_l/view_r -> optional<View{d_value, d_rest}>` exactly.
The contract is a **quotient contract**: the rest's element sequence is
specified, its shape is not.

**`examples/sequence_algorithms.cpp`** — the concrete demonstration that
quotient equivalence is all the sequence-facing algorithms require, with
each operation coming from its proper home. Construction and
decomposition derive from a five-operation interface (`empty`, `leaf`,
`concat`, `view_l`, `view_r` — no branch access, no measures, no
balance) expressed as an example-local concept: `cons`, `snoc`,
`from_range`, `reversed`, `sequence_equal`. Elementwise consumption
comes from **Foldable, not the views** (ruling, Steve 2026-07-17):
`fold_left` is derived from `fold_map` via the left-fold-program monoid
— the same `foldMap`-to-`foldl` derivation Paper C's Foldable base
ships as `LeftFoldProgram` — so there is one `fold_left`, owned by the
Foldable surface, and the raw `view_l` walk appears only as the law
check that the structural and sequential readings agree. The payoff
check: the same sequence built snoc-wise and cons-wise has observably
different shapes under a structural fold, while every
quotient-interface algorithm and every Foldable fold — monoid
associativity is precisely shape-independence of bracketing — agrees.
That is what licenses a representation to rebalance internally:
FingerTree's `concat` restructures aggressively, and nothing observable
through this interface can tell.

## 3. What the fingertree survey established (Paper C vocabulary)

Surveyed 2026-07-17 for vocabulary alignment before writing any of the
above (coherence-skill procedure: read the companion first).

- Decomposition: `view_l()` / `view_r()` members returning
  `std::optional<View>`, `struct View { T d_value; FingerTree d_rest; }`.
  Convenience `head/tail/last/init/front/back` derive.
- Ends: `cons` / `snoc`. Concatenation: member `append`, static
  `concat` (static calls member). Bulk: `from_sequence`, `leaf`.
- Monoid vocabulary is `identity()` / `combine()` via
  `Monoid<T>`/`monoid_v<T>` — matches the frozen spelling.
- **The quotient is implemented but unnamed**: `FingerTree::operator==`
  compares `flatten()` sequences (with an O(1) size short-circuit for
  unit measure) — element-sequence equality, not shape equality. No doc
  in that repo uses the word "quotient." Naming the concept is available
  work for the papers/essay.
- Foldable/Traversable are typeclass-lookup surfaces
  (`foldable_typeclass`, `traversable_typeclass`), not members; its
  Traversable materializes via flatten/from_sequence (O(n)), consistent
  with extraction-plan §6's honesty note.

Fringe-tree mirroring is deliberate and now includes the views. The
fringe tree remains the in-paper miniature; FingerTree the industrial
version; no normative dependence either way.

## 4. Decisions this opens (for Steve — [S-n] candidates)

1. **Status of the sequence-facing surface.** `view_l`/`view_r` on the
   fringe tree ship in `include/` as evidence (labeled as such in the
   header comment). Do views + the five-operation quotient interface
   become proposed Paper D surface, stay evidence cited by the paper,
   or move to Paper C as the generic face of FingerTree? Current lean:
   evidence in D's repo, prose in both papers, mechanism debate nowhere
   (there is no mechanism — the concept is five requirements). Ownership
   is genuinely C-adjacent; needs an explicit call.
2. **Naming the quotient.** "Persistent sequence up to quotient
   equivalence" is the design truth both repos implement (C's
   `operator==` already IS it). Adopt "quotient" as paper vocabulary?
   The relationships essay (extraction-plan §7a) is the natural home for
   the one-paragraph version.
3. **`operator==` for FringeTree.** Mirroring C's sequence-equality `==`
   onto the fringe tree would put the quotient into the type. Deferred:
   it edits the anchored ported class (transclusion churn) and is not
   needed by any current consumer — `sequence_equal` in the example
   covers the semantics. Revisit at the next anchor re-diff.
4. **Concept/algorithm names** in `sequence_algorithms.cpp`
   (`persistent_sequence`, `sequence_equal`, `reversed`, `from_range`)
   are placeholders, explicitly so in the file. If any are promoted,
   run the coherence-skill vocabulary sweep first. **Ruled (Steve,
   2026-07-17): `fold_left` is not a sequence-interface operation** —
   it is Foldable's derived operation, one definition derived from
   `fold_map`, and trees reach it by being Foldable. The example now
   derives it that way; no parallel view-based definition exists. This
   also keeps the coherence hazard honest: the Foldable derivations'
   home is the Foldable surface (transpose's `fold.hpp` / fingertree's
   `foldable.hpp` as evidence; Paper D proposes only the `fold_map`
   primitive), and nothing here grows a third one.
5. **Paper delta.** The draft's Fix story was adapter-flavored;
   ask 1's example warranted a short "Building on Fix directly"
   subsection (search tree lifecycle, the prunes-vs-folds division), the
   rose tree joins the implementation-experience matrix (four
   representations become five), and the traversal-order paragraph gains
   the no-generic-in-order argument with the rose/binary "123"/"213"
   contrast. **Drafted 2026-07-17** in `papers/algorithms-for-trees.md`
   (the paper's real number, assigned by Steve; supersedes DnnnnR0),
   together with a blog pass adding the direct verbs, fold_map, and the
   rose tree to `docs/recursive-tree-algorithms.org`.

## 5. Discipline compliance

- DEV-01: every new test/example algebra is order-sensitive
  (string/vector concat, digit probes); DEV-03: no bare CTAD on wrapped
  types anywhere new; DEV-04: constexpr at declaration on the new
  instances, `static_assert` coverage for the rose lifecycle and the
  BST lifecycle.
- Equivalence laws: rose `fold_map` lookup tier ≡ explicit tier
  (tested); rose layer-fold contract pinned per-layer; views pinned to
  `flatten()` (the quotient's ground truth).
- Frozen names respected: no `cata`/`ana`/`hylo`; `identity`/`combine`
  untouched; new identifiers (`RoseF`, `rose`, `view_l`, `view_r`,
  `FringeView`) checked against the naming invariants.
- Anchored regions: untouched; new UUID anchors added for the rose
  header regions and the fringe views (inventory updated).
- New surface stays plain callables/aggregates — usable as
  gcata/gana ingredients unchanged (extraction-plan §7 checklist).
