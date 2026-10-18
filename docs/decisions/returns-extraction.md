# What a Returns extracted from a body says

## Status

Accepted. Decided by the project owner, 2026-10-18 (issue #143).

## Context

The extraction marker for *Returns* was spelled `\returns-equiv`, and it
filled the same *Equivalent to:* block as `\effects-equiv`, from the whole
body. A body `return x + 1;` rendered as *Returns*: Equivalent to: followed by
a code block holding `return x + 1;`, and a body with several statements was
accepted under *Returns* too.

That combination means nothing in [structure.specifications]. *Effects*:
Equivalent to: says the function does everything the code does. *Returns*
names the value the function produces and says nothing about effects. A
*Returns* that is "equivalent to" several statements describes effects under an
element that does not carry them.

## Decision

The marker is `\returns-expr`. It reads a definition body that is exactly one
`return E;` and renders *Returns*: `E`, a bare expression rewritten like any
other extracted code: implementation qualifiers dropped and exposition-only
names rendered as such. The draft writes *Returns* this way throughout, for
example [unique.ptr.special]'s *Returns*: `x.get() == y.get()`.

- The `static_assert` run that the Mandates derivation consumes
  (`docs/architecture.md` §5.2) is not part of the body. It is already a
  *Mandates* element, not an effect.
- Any other body is an Error that points at `\effects-equiv`: a local, a local
  alias, a `return;`, or a second statement. Whatever else the body does is an
  effect, and only the Equivalent to: block says all of it.
- The old spelling `\returns-equiv` is an Error that names the new one. It is
  not kept as an alias.

## Consequences

- Lowering leaves no placeholder for `\returns-expr`. The front end builds the
  *Returns* element, because only it can see whether the body is one return
  statement.
- Renaming a marker without an alias is a breaking change to input, and is
  acceptable before 1.0 only. From 1.0, a marker's spelling is stable.
