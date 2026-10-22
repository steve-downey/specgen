# Visitation rules

## Status

Accepted

## Context

`if constexpr`-chain dispatch over variant alternatives (the shape of the
LaTeX renderer's `render_node` and of `emit_json(Node)`) silently falls
through on a new alternative and invites copy-pasting. The sibling `cl`
repository's coding rules contain visitation rules that close that hole; they
are imported into this project's CODING_RULES.

## Decision

House rules, imported from the sibling `cl` repository's coding rules:

- `overloaded` (the tripwire variant, with the consteval `static_assert(false)`
  exhaustiveness check) is the visitor builder: a forgotten variant
  alternative is a compile error naming the case, not a silent fall-through.
- `std::visit` may dispatch **one node's alternatives inside an algebra**; it
  must not drive recursion — recursion belongs to `fold_with`.
- The other sum type in the tree, the enumeration, gets the same treatment
  from `foundation/enumeration.hpp`: `enumerators<E>` states the summands once,
  `table` is a total enumerator-to-value lookup and `match` runs a case, and
  a missing or repeated enumerator is a compile error naming it. A `switch`
  over an enumeration is a defect (`style.no-raw-switch`); `-Wswitch` alone is
  not the gate, since a `default:` or a fallback `return` silences it.
- Overloaded-lambda sets for ≤3 stateless cases; **named visitor structs**
  (a doc comment per case, a name in diagnostics, member state instead of
  captures) above that or when stateful — which covers every backend visitor
  and the JSON descriptor walkers.

## Consequences

- The `if constexpr`-chain dispatch sites are replaced, and a third
  copy-paste of that shape is forestalled.
- Adding a variant alternative breaks the build at every visitor that fails to
  handle it, naming the missing case.
- Backend visitors are named structs by rule
  (see [backend-direct-algebra](backend-direct-algebra.md)).
