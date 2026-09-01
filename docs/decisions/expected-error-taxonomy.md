# Error taxonomy: expected with and_then; diagnostics as a monoid

## Status

Accepted

## Context

Without a house rule, fallible code mixes `if (!r.has_value()) return r;`
ladders, `optional<T> + ParseError*` signatures, and ad-hoc diagnostic
collection. The
sibling `cl` repository's coding rules contain a worked error taxonomy that
fits; adopting it verbatim as house rule gives every fallible shape one
sanctioned spelling.

## Decision

Adopt the sibling `cl` repository's error taxonomy verbatim as house rule:

- **A single fallible step** → `expected<T, E>` + `and_then`.
  `if (!r.has_value()) return r;` ladders are outlawed.
- **A sequence that must stop early** → `fold_left_short`
  (`std::ranges::fold_left` cannot stop early, which is why the ported header
  exists).
- **A structure to thread the effect through** → concrete `traverse` overloads
  in `foundation/traverse.hpp`
  (`vector<expected<T,E>> -> expected<vector<T>,E>` and the `NodeF` analogue)
  — not a Traversable framework
  (see [no-typeclass-objects](no-typeclass-objects.md)).
- **Where multiple diagnostics must be reported** (docblock parsing does this
  via `ParseResult`; validators do too) → *Validation* style, not fail-fast:
  carry `{value, vector<Diagnostic>}` and combine diagnostics with the
  `Diagnostics` monoid (`combine` = concatenation, `identity` = empty).
  `ParseResult` is already this shape; the rule names the pattern rather than
  inventing it.

Assignment: IR JSON parsing and `parse_rsec` are fail-fast expected with a
position (in a serialization format the first error is definitive); docblock
and validators accumulate; driver/frontend orchestration is an `and_then`
pipeline over the stages.

**Carrier.** Evaluate **`steve-downey/expected`** — the implementation with
`T&`/`E&` reference support — as an early spike; subtree it if adopted
(reference support lets `and_then` chains pass large IR pieces without
copies). `std::expected` (C++23, in libstdc++ 16) is the fallback if the spike
shows the reference support going unused. The vendored helpers are written against
the chosen carrier once, up front.

## Consequences

- A validator is `fold_with<Diagnostics>` over `NodeF` with the diagnostics
  monoid: leaf cases check `SpecItem`/`Synopsis` locally and return findings;
  the `SectionF` case concatenates child results and prefixes each with its
  `stable_name` context — inherited context achieved in a plain fold because
  the parent post-processes child results. `validate(doc) -> Diagnostics`;
  severities decide the exit code. New validation rules are pure
  algebra-writing over this skeleton.
- Every fallible signature in the tree has one of four sanctioned shapes, so
  review can flag deviations mechanically.
