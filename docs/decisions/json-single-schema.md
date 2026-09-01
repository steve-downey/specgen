# JSON: one description of the schema drives both directions

## Status

Accepted

## Context

A hand-written emitter (~280 lines, ten `std::exchange(first, false)` comma
sites) and a parallel `Reader` recursive descent (~250 lines) duplicate the
IR's JSON schema with no single source of truth, while the IR is still gaining
content. Every addition is a multi-place edit with silent drift risk, and the
emitted format — key order included — is contract.

## Decision

Replace both directions in two stages:

1. **`foundation/json_writer`** — RAII object/array scopes own comma
   placement. This kills the comma logic independently of everything else,
   with JSON bytes identical.
2. **Per-IR-type member descriptor tables** —
   `static constexpr auto members = std::tuple{field("begin", &Span::begin), …}`
   with type-driven handling of `string`, `size_t`, `bool`, `vector<T>`,
   `optional<T>`, enums (via the existing `element_name`-style maps), and
   tagged variants (`Node`, `Inline`: an
   `alternatives("type", alt<Section>("section"), …)` descriptor matching the
   `"type"` key). Generic `emit_json_described` / `parse_json_described` walk
   the same table, so the schema exists in one place and round-trip
   holds by construction. The `Reader`'s lexer core stays; its per-type
   walkers go.

**Bail-out gate:** if the descriptor machinery exceeds ~300 lines, stop at
stage 1 and table-drive only parse key-dispatch.

## Consequences

- Adding an IR field is a one-place edit to a descriptor table; emit and parse
  cannot drift apart because they walk the same table.
- An external JSON library is rejected: a dependency for a twenty-key schema,
  and the emitted format — key order included — is contract that a generic
  library would not preserve by construction.
