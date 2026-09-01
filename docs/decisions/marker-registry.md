# One marker registry

## Status

Accepted

## Context

Describing the docblock markup's marker set in more than one place — the
grammar's `Markers` struct, a fourteen-field hand copy as
`lowering::ItemDirectives` in `lower.cpp`, and the `MarkerInfo` table used by
`find_marker` — makes adding a marker a multi-file edit with drift risk
between the copies.

## Decision

`grammar::Markers` moves to a shared header as the single struct;
`lowering::ItemDirectives` becomes an alias for it, deleting the
fourteen-field hand copy in `lower.cpp`. The existing `MarkerInfo` table
(`bool Markers::*` pointer-to-member plus flags) is promoted to *the*
registry — one `constexpr` array of `{spelling, member-or-setter, arity}`
entries driving `find_marker`, the marker cross-checks, and any future
serialization.

## Consequences

- Adding a marker is a one-line, one-file change.
- No X-macros: the pointer-to-member table already in the code is the right
  mechanism — it just has to be the only one.
