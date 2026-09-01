# Anchor inventory — transclusion contract (Tasks 3a/3b → 4.4)

Complete UUID-anchor map for docs/recursive-tree-algorithms.org and papers/algorithms-for-trees.md (formerly DnnnnR0).
"Serves" line numbers refer to the article's TRANSCLUDE-TODO markers.

## Fold family additions (2026-07-14)

| Anchor | File | Region | Serves |
|---|---|---|---|
| 6c596985-e990-4808-a504-2b651c34cebc | include/.../recursion_schemes.hpp | fold_with | paper synopsis; essay exhibit E3 candidate |
| d160af7b-9a3c-4593-b8f5-fc1ff0404b37 | include/.../recursion_schemes.hpp | unfold_with | paper synopsis |
| 52588460-d05a-43f9-97cc-01aa06732730 | include/.../fold_map.hpp | fold_map over Fix (explicit) | paper synopsis |
| 5bf78776-81e7-4b66-8487-6615adc33b17 | include/.../fold_map.hpp | fold_map direct (explicit) | paper synopsis |
| 94d2dd48-8976-43fc-943d-0dd27177e41a | include/.../fold_map_lookup.hpp | layer_fold/project lookup variables + traits | paper synopsis (lookup) |
| e4beac7f-257b-4f2d-bfff-6b3b5daf61ad | include/.../fold_map_lookup.hpp | fold_map over Fix (lookup) | paper synopsis (lookup) |
| 7f08b12d-3d26-4b5e-bf79-66d38f6fee0d | include/.../fold_map_lookup.hpp | fold_map direct (lookup) | paper synopsis (lookup) |
| 79aa4002-fa4a-472e-b01b-f580a13f60ec | include/.../binary_tree.hpp | BinaryTreeLayerFoldMap | follow-up article |
| c9e514d9-4cf1-4fa1-ac4d-a9dfdf040291 | include/.../binary_tree.hpp | BinaryTreeProjectFn + lookup registrations | follow-up article |
| 1484acca-10e3-4500-93e1-092ccec4e3f6 | include/.../fringe_tree.hpp | FringeTree class | follow-up article |
| 0a841db5-7eea-4a8c-bda8-64e9c015a32e | include/.../fringe_tree.hpp | FringeEmpty/FringeLeaf/FringeBranch + FringeTreeF | follow-up article |
| a692305b-eb87-43fb-98a0-901a582d7ddf | include/.../fringe_tree.hpp | functor Impl/Map + specialization | follow-up article |
| 321edf34-488f-4322-ac5c-320fa5c4d1fc | include/.../fringe_tree.hpp | project + embed (measure invariant) | follow-up article; measures bridge |
| e6cd1dd3-85b1-4ce2-bf7f-2ab1783d16f8 | include/.../fringe_tree.hpp | FringeTreeLayerFoldMap + lookup registrations | follow-up article |
| 8f25579d-443a-4685-b3af-01f8103f6863 | include/.../expression.hpp | ExprLayerFoldMap (6-vs-9 contrast) | paper §Folding; essay exhibit E4 candidate |
| 04e450a4-3614-4f09-a185-1835f935673a | examples/nonce_tree_direct.cpp | nonce Node + NodeF + fmap + project | paper §Trees you don't own; essay exhibit E3 |

## Rose tree and fringe views (2026-07-17)

Added with `docs/notes/representations-and-sequence-interfaces.md`.
Consumed 2026-07-17: the article transcludes 8f0e2d0a and 4b9d17f2 (§Not
Every Tree Has an In-Order), and D4322R0 carries the §4.5 paper delta as
hand-mirrored prose (rose tree in implementation experience, the
no-generic-in-order argument in §Folding). The article's 2026-07-17 pass
also consumed 6c596985/d160af7b (fold_with/unfold_with),
e4beac7f (fold_map lookup), 8f25579d (ExprLayerFoldMap), and
04e450a4 (nonce ingredients) — previously marked "follow-up article".

| Anchor | File | Region | Serves |
|---|---|---|---|
| 8f0e2d0a-5c1f-4be4-9f1e-3b7a8c92d461 | include/.../rose_tree.hpp | RoseF + Layer/Fix aliases + rose() | paper implementation-experience (fifth representation) |
| 0d3c6e8b-92f4-45f7-8a26-6c1de5b0a973 | include/.../rose_tree.hpp | functor Impl/Map + specialization | spare |
| 4b9d17f2-30a5-4e1c-b8d4-7f52a6c3e08d | include/.../rose_tree.hpp | RoseLayerFoldMap + lookup registration | paper traversal-order contract (no generic in-order) |
| f4f21f0a-6b7e-4f0d-9a3c-58f1c2d7ab26 | include/.../fringe_tree.hpp | FringeView + view_l/view_r | sequence-quotient story; essay candidate |

## Examples (Task 3b)

| Anchor | File | Region | Serves |
|---|---|---|---|
| ec8e7f28-10c3-43ea-bfa9-a54f4c63ef9c | examples/fixpoint_tree_example.cpp | compact Box + make_box | article L34 |
| 3959d7ab-f1fa-4fd7-b2b9-886a1f7610a2 | examples/fixpoint_tree_example.cpp | expression type system | article L69 |
| 02640f07-f080-4eec-bd90-b414fabd4b7b | examples/fixpoint_tree_example.cpp | smart constructors | article L91 |
| 2526ec45-2e52-4b56-aab0-1034b078a459 | examples/fixpoint_tree_example.cpp | main + one-layer eval + local fold | article L110 (region includes working local fold; trim decision at wiring time) |
| a66c0122-f24d-4858-a802-cf76f87969e9 | examples/expression_algorithms.cpp | example-local Foldable/Traversable maps | follow-up article (spare) |
| 868a8228-44a7-4761-8822-28b622d6b9a0 | examples/expression_algorithms.cpp | validate_impl | algorithm-object story |
| 7226de56-d7ce-44c6-9e99-849da548d3e4 | examples/expression_algorithms.cpp | transform_if_large_impl | algorithm-object story |
| fba6bbba-6cd8-4f4f-ba18-b584cf672a79 | examples/binary_tree_adapt.cpp | to_fix + shape algebra + fold + from_fix | article TODO(3a) L714 |

## Library headers and tests (Task 3a)

| Anchor | File | Region | Serves |
|---|---|---|---|
| a1f81cb9-ebb3-4872-9549-03ca140c61b2 | include/.../box.hpp | Box class | article L185 (first half) |
| 45a64b08-8217-4baf-b19b-146434de1e44 | include/.../box.hpp | make_box | article L185 (second half; concatenate both anchors) |
| 57d4bd6e-c8c7-4806-afd3-2e42aec8ae27 | include/.../fix.hpp | Fix struct | article L255 |
| 53775b7e-8a78-4b79-885b-046f6232d7a3 | include/.../fix.hpp | wrap_fix + unwrap_fix | article L266 |
| 085bb189-a48e-4262-aefd-b64f8755e959 | include/.../expression.hpp | ExprF base functor | article L150 |
| a052ddcb-f05b-41c9-85bf-4c443ab438b7 | include/.../expression.hpp | functor Impl/Map + specialization | article L328 |
| d0f9b5a2-363f-49ca-9a37-bfb0e6c9ff50 | include/.../expression.hpp | smart constructors | article L380 |
| bbf73c10-b63e-434a-91f1-d1a3a7c23b96 | include/.../expression.hpp | eval_algebra | article L422 |
| 66235297-8e2a-4610-b6a4-a3f2a8837fb0 | include/.../expression.hpp | eval function | article L543 |
| 7970dd71-b5dd-4fa0-8e80-735a26833b65 | include/.../functors.hpp | NatF family | article L625 |
| edbdeea5-9b3e-4d9a-88d5-fb2610c9174d | include/.../functors.hpp | NatF functor instance | spare |
| f419286e-ec56-4291-9f09-d5528685805f | include/.../functors.hpp | make_zero/make_succ/nat_from_int/nat_to_int | article L638 — RETARGET: article says recursion_schemes.test.cpp; coalgebra lives inline in nat_from_int |
| f6f4cee4 | include/.../binary_tree.hpp | BinaryTree class | article L714 section |
| 27aeff53 | include/.../binary_tree.hpp | BinaryTreeF + Layer/Fix aliases | article L714 section |
| 91fef612 | include/.../binary_tree.hpp | functor instance | article L714 section |
| a7ed3ac1 | include/.../binary_tree.hpp | to_fix/from_fix + trait | article L714 section |
| 5033568c-a02f-44b0-964b-7076ebb06d43 | tests/.../expression.test.cpp | build (1+2)*3 | article L403 |
| c1858641-8184-4907-ad06-14744a4ac1d2 | tests/.../expression.test.cpp | print_algebra fold | article L560 — RETARGET: article says recursion_schemes.test.cpp |

## Missing anchors (comment-only additions in include/, owned by Task 4.4)

- functor.hpp: `functor_typeclass` variable-template default → article L316
- recursion_schemes.hpp: `fold_fix` explicit-fmap overload → article L448
- recursion_schemes_lookup.hpp: `layer_fmap` → L511; `fold_fix` → L523; `unfold_fix` → L604; `refold` → L660
- papers/D4322R0 synopsis is hand-mirrored (see its HTML comment) against the verb-signature anchors above; re-diff on any signature change.

## Snippet gate

`paper_snippets_check` custom target (top-level CMakeLists.txt, ALL) depends on the three anchored examples + tests.{box,fix,expression,functors,binary_tree}; negative-tested (injected syntax error fails the build).
