# A foreign name spelled like one this run declares

## Status

Accepted

## Context

Design §9's bare-name leakage clause
([#84](https://github.com/steve-downey/specgen/issues/84)) resolves a
reference and then reports it by text. The front end records every reference
whose declaration lives outside the document and outside a system header
(`ir::ForeignDeclaration`), and the validator reports each *occurrence* of
that spelling in rendered output — because locating occurrences is a text
match, and the recorded fact carries no position of its own.

A spelling can name two entities. `beman.transcode` has an `enum class codec`
whose enumerators are encoding names, and generated lookup tables named after
the encodings they decode; `codec::windows_1252` and
`detail::tables::windows_1252` are different declarations sharing a word, on
purpose, because both are named after the encoding. The table is reached from
a function body the tool never renders, which is what puts it in the run at
all. The check then fired three times against a document that was otherwise
at zero, one of them on a *qualified* use in a `\remarks` and the first of
them at the enumeration's own declaration
([#93](https://github.com/steve-downey/specgen/issues/93)): reporting an
enumerator as undocumented at the point where the enumeration that documents
it is being rendered.

The validator already declines to report a name in the roster's `documented`
set, and that guard is why this took a report to find. It does not reach here.
A coverage roster records an enumeration; its enumerators are text inside an
itemdecl, so the run genuinely did not know it declared that spelling.

The offered fixit — mark the table `\expos` where it is declared — silences
the finding and changes no rendered output. It also asserts that the table is
an exposition-only entity *of the specification*, which it is not: it is a
generated file the document deliberately does not reach. In a library with
thirty such tables that is thirty false claims, written to quiet a text
match.

## Decision

**A name this run declares wins.** A spelling declared anywhere in the
document files is never recorded as a foreign declaration, however the
reference resolved.

The filter is in the front end, in `collect_foreign_declarations`, beside the
walk that records the foreign names: a second pass over the same declarations
collects every `NamedDecl` written in the document files, and a collected
foreign name whose spelling collides is dropped. The validator is unchanged,
and `ir::Document::foreign_declarations` may be read as "no entity of this run
answers to this name".

The front end is where this belongs because the question is *where a
declaration lives*, which is the one thing the IR boundary does not carry.
Answering it validator-side means a fourth document channel listing every
declared name — a key in every `tests/golden/*/expected.json` for a fix that
changes no wording, and a second consumer of the same fact. Filtering at the
source moves only the goldens where a collision actually exists.

The rule is deliberately wider than the `documented` guard it repairs. It also
covers a private or `\omit`ted member sharing a spelling with a foreign
declaration, which is not "documented" by any reading. That is the direction
to be wrong in, and it is the direction every other §9 rule already errs in: a
name this run declares is this run's to explain, whether it explains it well
is a question the roster checks are *already* asking about that same
declaration by disposition, and a foreign entity that happens to share the
word is evidence about neither.

Everything else about the clause stands. The qualifier half is untouched — a
written `detail::` in rendered output is wrong whatever it qualifies, and
`foreign_namespaces` still records the namespaces a withheld name sat in. A
foreign name that collides with nothing this run declares is still reported,
which is what `tests/corpus/support/spec_foreign_detail.hpp`'s `detail::eval`
and `probe_t` keep pinning.

## Rejected: scope the channel to the document extent

The report's second suggestion was to record nothing about headers outside
the document extent — `detail/tables/windows_1252.hpp` is not included inside
the gathered `.syn` region, so
[document-extent](document-extent.md) already calls it invisible.

That reading of the collector's scope is one release out of date. The scope
is not "a main header plus whatever it transitively `#include`s": it is
`DocumentFiles`, which *is* the document extent (the main file plus its
gathered `.syn` follows,
[#77](https://github.com/steve-downey/specgen/issues/77)), and "foreign" is
already defined as *outside it*. Scoping the channel to the document extent
therefore leaves it with an empty domain: every finding it can ever produce
is about a declaration outside the extent, because that is the definition.
It would silence `tests/golden/foreign_include_validate/expected.diag`
entirely and amount to reverting #84.

The suggestion is answered rather than deferred: what it asks for — that the
tool say nothing about a header the author has declared to be implementation
— is the *fixit* side of the same clause, and it is already `\expos`'s job.
The report's real objection was to being made to write `\expos` on a
declaration the specification never mentions, and withholding the shared
spelling removes the demand at its source.

## Consequences

- A library may name an enumerator and a generated table after the same
  encoding, which is what the encodings make natural, and neither has to be
  renamed or annotated to suit the generator.
- The bare-name clause loses findings it would have made against a document
  that reuses a foreign spelling for its own entity. It never invents one,
  which is the property every other §9 clause has and this one, being
  resolved but reported by text, did not.
- `tests/corpus/spec_shared_spelling.hpp` is the fixture, and its finding
  count is zero: the golden's ordinary `.validate` sibling is the assertion.
  A regression re-opens it as three findings, one of them on the enumeration
  itself.
- Nothing in the IR says which names the run declares, so this question can
  only ever be settled in the front end. `validate.test.cpp` pins that
  boundary from the other side: an enumerator is no roster row, and the
  validator alone still reports the shared spelling.
