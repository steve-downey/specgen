# Parser combinators for the hand scanners

## Status

Accepted

## Context

The section-heading scanner and the docblock grammar are hand-rolled
character-walking code, including a latent `std::stoi` out-of-range throw. The
docblock grammar is line-oriented markers plus backtick inlines — squarely in
a parser combinator library's sweet spot — and `compile-time-scheme` provides
a combinator core that can be runtime-ized.

## Decision

`foundation/parse/{cursor,parser}.hpp`, runtime-ized from
`compile-time-scheme`'s parser: immutable `cursor` (`bump()` returns a new
cursor; offset/line/column ride along, so parsers checkpoint and backtrack
freely), `parse_state<T>{value, rest}`,
`parse_result<T> = expected<parse_state<T>, parse_error>`, primitives `pure` /
`satisfy` / `char_p` / `keyword` / `map` / `lift2` / `sequence_left` /
`sequence_right`, `operator|` as ordered choice with
**commit-on-consumed-input** (the error's position doubles as the commit
signal — a committed branch's failure is final), `many` / `some` / `opt` /
`lexeme` collecting into `std::vector`, plus a checked `digits` (no
`std::stoi`).

The hand scanners are rewritten with it in blast-radius order: `parse_rsec`
first — which also converts the latent `std::stoi` out-of-range throw into a
positioned parse failure — then `strip_comment_decorations` /
`parse_inlines` / `parse_docblock`. The docblock grammar's three-severity
diagnostic accumulation is preserved
(see [expected-error-taxonomy](expected-error-taxonomy.md)).

## Consequences

- Parse failures carry positions, and backtracking is free because cursors are
  values.
- Numeric conversion is checked at the grammar level; no scanner can throw out
  of a parse.
- New grammar productions compose from the primitive set instead of extending
  hand-rolled loops.
