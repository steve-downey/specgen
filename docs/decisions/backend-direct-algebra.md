# Backend seam: a direct algebra per backend

## Status

Accepted

## Context

A streaming renderer threads loop-carried state — `bool& first` flags and
`&para == &element.paragraphs.front()` identity tests — through every level of
the tree walk, and each new backend copies that machinery. With the base
functor in place (see [node-base-functor](node-base-functor.md)), a backend
can instead be a fold algebra straight to its output.

## Decision

Each backend is a self-contained **named visitor struct** (per
[visitation-rules](visitation-rules.md)) used as a `fold_with` algebra straight
to its output — no intermediate document/layout type. The mechanics:

- **Separators by joining, not flags.** The `SectionF` (and element-level)
  cases receive their children's already-rendered results and join them with
  blank-line separators (`views::join_with`-shaped). This deletes every
  `bool& first` thread and front-element identity test structurally;
  separation becomes a property of the join.
- **Inherited attributes via a reader-style carrier.** Section depth (and the
  paragraph counter the org backend needs) flow *down*, but a fold computes
  *up*; so the algebra's carrier is a small callable invoked with
  `RenderCtx{int depth, int pnum_base}` at the root. Only the `SectionF` case
  consults or increments depth, so the closure surface stays tiny; leaf cases
  return constant functions of the context.
- **One shared substrate.** The span-substitution walk over `CodeText::spans`
  and the per-backend escape hooks (`\exposid{…}`/`@…@` versus markdown) live
  in `backend/common.hpp`, written once and marked
  `// substrate generic algorithm`; each backend supplies its escape functions
  to it.

The LaTeX backend is re-founded on this shape first and gated byte-exact
against its goldens — the acid test. The mpark and org backends are then new
visitor structs over the same `NodeF`, inheriting the join and span substrate.

## Consequences

- Adding a backend means writing one visitor struct over `NodeF`; the joins
  and the span-substitution walk are already written.
- An intermediate `Doc` block layer (Text/VCat/Section) with one shared layout
  walk is declined: it centralizes separator logic further, but adds a type
  between IR and output that the direct form proves unnecessary. A full
  Wadler/Hughes pretty-printer is also declined — line-filling has no client,
  since synopsis code arrives pre-formatted by `clang::format`.
