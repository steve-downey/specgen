# Names supplied outside generated sibling wording

## Status

Accepted

## Context

Paper-wide validation unions the normative entities rendered by every
`--from-ir` input (issue #109). Two declarations deliberately produce no node:

- a declaration whose wording is hand-authored elsewhere in the paper; and
- a namespace `using` declaration carrying `\expos`, which introduces a name
  but is not itself an exposition-only declaration the draft should print.

Treating every `\omit` declaration as paper-visible would silence the report,
but it would also turn an exclusion marker into a claim that implementation
machinery is normative. For a `using` declaration, ignoring the marker is worse:
the leakage diagnostic recommends adding the `\expos` already present.

## Decision

`\elsewhere` is the explicit spelling for a declaration supplied by hand
outside generated wording. Generation suppresses it exactly as it suppresses
`\omit`, while the front end records its semantic name in
`Document::paper_entities`. The marker is mutually exclusive with `\omit`,
`\merge`, and `\expos`.

A namespace `using` declaration carrying `\expos` also contributes its
introduced name to `paper_entities`. The exposition map records both Clang's
`UsingShadowDecl` and its underlying target, because different reference forms
resolve to different sides of that pair. The `using` declaration emits no
synopsis node; reached uses are rewritten to the ordinary exposid spelling.

The serialized `paper_entities` field is optional and omitted when empty, so
older IR remains readable and unrelated documents retain byte-identical JSON.
`validate::documented_names` includes the field in the set the driver unions
across all inputs. Validation of the declaring document trusts the same
explicit promise.

## Consequences

- `\omit` keeps its narrow meaning: suppress this declaration, with no claim
  about hand-authored wording.
- A split paper can name a hand-authored declaration without gathering every
  header into one `.syn` document.
- Marking a namespace `using` declaration `\expos` is effective even when the
  marker lives in an included sibling header.
- `paper_entities` carries semantic identity rather than recovering names from
  formatted output, preserving the AST-for-structure rule.
