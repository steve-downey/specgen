# Consume sibling code via git subtree, plus an adapted foundation

## Status

Accepted

## Context

The recursion verbs and the parser-combinator core come from sibling
repositories (`tree_algorithms`, `compile-time-scheme`, `fixpoint`,
`transpose`) that are still moving. Some pieces can be consumed verbatim; some
need runtime adaptation and therefore diverge by construction. Both drift and
usage friction must stay visible.

## Decision

Two mechanisms, chosen per piece:

- **`git subtree add --prefix vendor/tree_algorithms <repo> main --squash`**
  for the recursion verbs. `recursion_schemes.hpp` (`fold_with`,
  `unfold_with`, `fold_fix`, `unfold_fix`, `refold` — frozen naming, never
  cata/ana/hylo) is consumed directly from the subtree include path,
  explicit-parameter tier only
  (see [no-typeclass-objects](no-typeclass-objects.md)). Subtree pulls refresh
  it; local edits to vendored files are forbidden — friction becomes a log
  entry and an upstream issue instead.
- **Ported (not subtree'd) into `include/beman/specgen/foundation/`**, with a
  provenance header naming source repo, commit, and original path, for the
  pieces that need runtime adaptation and therefore diverge by construction:
  the parser-combinator core from `compile-time-scheme`'s
  `src/smd/smdscheme/parser/` (drop the `Capacity` NTTPs and `static_vector`,
  collect into `std::vector`, expected carrier per
  [expected-error-taxonomy](expected-error-taxonomy.md), add a checked
  `digits`), `fold_left_short` (rewritten against expected), and `overloaded`
  with the consteval `static_assert(false)` exhaustiveness tripwire from
  `src/smd/fixpoint/overloaded.hpp` — `tree_algorithms`' own `overloaded.hpp`
  lacks the tripwire, which is the first divergence-log entry and upstream
  suggestion.
- **`foundation/DIVERGENCES.md`** logs every friction point, adaptation, and
  wish. This *is* the usage-experience feedback channel to the sibling repos;
  logged frictions become filed upstream issues.
- `transpose` contributes ideas only (the traverse shape for validation, the
  monoid shape for diagnostics), no code. Its typeclass framework and
  `tree_algorithms`' lookup tier are self-described temporary stubs of the
  WG21 proposal P3200; "the explicit tier sufficed for a real tool" is itself
  feedback.

## Consequences

- Vendored code keeps an update path (subtree pulls) and drift stays visible;
  plain file copies would lose both.
- A FetchContent/vcpkg dependency is rejected: it imports the sibling repos'
  churn into specgen CI while they are still moving, and
  `compile-time-scheme` is not consumable as a library at all. Revisit when
  `tree_algorithms` freezes its API behind a released port.
- Every adaptation in `foundation/` is traceable to its source and its reason
  via the provenance header and the divergence log.
