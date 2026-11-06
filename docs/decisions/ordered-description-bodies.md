# Description bodies preserve authored order

## Status

Accepted

## Context

The separate paragraph, list, table, and extracted-code slots cannot represent
prose after a list or interleaved tables. Backends impose an order and merge
slots independently, so storage alone cannot preserve an author's wording.
Adding a new JSON field is also unsafe: existing readers skip unknown fields
and could render a declaration while silently discarding its description.

## Decision

A description element carries one ordered body of semantic blocks. Paragraphs,
itemizations, tables, and extracted `EquivalentTo` blocks are alternatives.
Derived conjunct evidence stays separate from rendered content. Same-kind
merging concatenates complete bodies in stable order.

Later additions use this same body for leading unlabelled wording, authored
code, notes, and examples. Authored code has no fabricated `Equivalent to:`
introduction. Informative bodies undergo span, reference, and leakage checks;
their text does not discharge normative drift obligations or assert normative
Throws guarantees. Recursive informative bodies use an explicit fold substrate.

Serialization remains descriptor-driven. A temporary wire representation reads
the old slots and normalizes them into the single ordered model, in the old
rendering order. It emits only the new body. Emitted item nodes use `item-v2`;
new readers also accept legacy `item` nodes. This makes old readers fail visibly
on every item containing the changed description contract, including class
wording without signatures. There is no second mutable body in semantic IR.

## Consequences

Existing IR remains readable; newly emitted IR requires an updated renderer.
Block order is part of the contract. Paragraph continuation after a list is
represented separately from an authored new numbered paragraph, so the default
rendering reproduces the draft's paragraph boundaries.
