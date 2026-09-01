# Explicit-parameter tier only; no typeclass-object framework

## Status

Accepted

## Context

`tree_algorithms` ships a two-tier doctrine: a primary tier where every verb
receives its operations explicitly, and a lookup tier of typeclass objects.
`transpose` carries CRTP bases and `*_typeclass` lookup. Both repos label the
lookup tier a temporary stub of the WG21 proposal P3200.

## Decision

Adopt `tree_algorithms`' two-tier doctrine and take **only the primary tier**:
every vendored verb receives its operations (`fmap`, `project`,
`combine`/`identity`) as explicit parameters. Do not vendor `transpose`'s CRTP
bases and `*_typeclass` lookup, nor `tree_algorithms`' lookup tier.

## Consequences

- specgen has about four functor-shaped types (`NodeF`, `vector`,
  `optional`/expected, and the diagnostics carrier) and on the order of ten
  call sites; explicit parameters cost less than a framework, and the
  restraint is itself feedback to the sibling repos.
- Monoids are a two-field `{combine, identity}` passed explicitly, matching
  `fold_map`'s primary spelling upstream.
- Revisit when P3200 (or `transpose`'s evolution) stabilizes; moving explicit
  call sites to lookup later is mechanical.
