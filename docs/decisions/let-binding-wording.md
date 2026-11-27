# How a derived "Let" binding reads

## Status

Accepted. Decided by the project owner, 2026-11-26 (issue #404).

## Context

A body that opens with type aliases followed by `static_assert`s derives
*Mandates* that name those aliases (`docs/architecture.md` §5.2). Before this
decision, nothing in the generated wording introduced an alias before that
*Mandates* paragraph used it. The alias stayed as the first line of the
*Effects* code, which comes after. The draft's form is a leading paragraph,
"Let `U` be `invoke_result_t<F, decltype((val))>`.", with no alias in the code,
as in [optional.monadic] and [optional.ref.monadic].

Deriving that paragraph raises four questions of wording. The project owner
answered them in an interview. The first was settled by sampling the draft:
the owner's rule is to follow current practice, and where the library's
wording added since C++23 is mixed, to use its most common form.

## Decision

A leading alias that a consumed `static_assert` names, directly or through
another alias, is introduced by a derived "Let" paragraph and leaves the
extracted *Effects* code, as the consumed assertions do.

### Several aliases

Every alias is bound in one paragraph, with one "Let":

- one alias: "Let `U` be `X`."
- two: "Let `U` be `X` and `V` be `Y`."
- three or more: "Let `U` be `X`, `V` be `Y`, and `W` be `Z`."

The aliases appear in source order, so one built from another follows it.

The draft does this inconsistently, and the practice has changed over time. A
scan of the library clauses at the 2026-07 draft (`45b03900`) found sentences
that bind two or more types together:

| Form | Present at C++23 (N4950) | Added since |
| --- | --- | --- |
| One "Let", bindings joined: "Let `U` be X and `T` be Y." | 2: [time.duration.cast], [time.parse] | 4: [meta.define.static], [exec.continues.on], [exec.bulk], [exec.connect] |
| A "let" per binding: "let `T` be X, and let `D` be Y" | 5: [meta.trans.other], [range.take.overview], [range.drop.overview], [range.counted], [range.as.const.overview] | 2: [exec.on], [exec.stopped.err] |
| A "let" per item of an itemization | 0 | 2: [exec.spawn], [exec.spawn.future] |
| Separate sentences in one paragraph | 1: [allocator.uses.construction] | 0 |
| A paragraph per binding | 1: [variant.helper] | 0 |

The separate-"let" form was the most common one at C++23. Since then the
joined form has been, though the sample is small and mostly from [exec].
Nothing added since C++23 gives each binding its own paragraph. A single
binding is always its own sentence, "Let `U` be `X`.", in every period.

### Placement

The derived paragraph comes first in the description, ahead of any leading
paragraph the author wrote. The author's prose may then use the alias.

### An authored "Let" that does not match

When the author's leading prose already has a paragraph opening "Let `U`",
that paragraph introduces `U`, and no paragraph is derived for it. The alias
leaves the code only if the paragraph reads exactly "Let `U` be `X`." with the
alias's own type, compared after the qualifiers the wording drops are removed.
Otherwise the alias stays in the code and a drift warning says the authored
binding does not match it. "Let `U` be the type `X`." keeps the alias and
warns.

### Scope

Only an alias the assertions name, directly or through another alias, is
derived. An alias that only the body uses stays in the *Effects* code, where
the code that uses it can be read.

## Consequences

- The derived paragraph is new output for an existing input: a minor version.
- One paragraph holds all the bindings, so adding a second alias to a body
  changes the existing paragraph's text, not the paragraph count.
- The drift warning on a mismatched authored "Let" is a new finding for headers
  that already carry one. Writing the paragraph exactly, or deleting it and
  letting it be derived, clears it.
- The choice of the joined form is a position on the draft's mixed practice.
  It is shown to LWG as a worked example (issue #439).
