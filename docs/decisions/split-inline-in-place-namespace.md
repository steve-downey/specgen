# split_inline keeps definitions in the class's own namespace block

## Status

Accepted

## Context

specgen's input contract (`docs/architecture.md` §1) wants a declaration-only
class body with the definitions out of line after it. A header written the
ordinary way defines its members in class, and restructuring it by hand has
two classic failure modes: the definitions go into a *reopened* namespace,
where a free function defined under a misspelled namespace or outside an
`inline namespace` silently becomes a new function; and a hidden friend is
moved out with the rest, which makes it visible to ordinary lookup and so
changes the interface. A converter that reads `//!` comments inside specgen's
own process would also have to share or contradict specgen's three-way
comment classification, and either is confusing to the author reading a
diagnostic. The plan is `docs/plans/split-inline.md`.

## Decision

`split_inline` is a separate executable, built from this tree on
`beman::specgen.frontend`, not a `specgen` command. It writes each moved
definition after the class *inside the namespace block that already encloses
it*: nothing is reopened, so the misspelled-namespace error can not be
written. A friend defined in the class, and a `= default` or `= delete`
member, is never moved. Without `--move-comments` every comment stays with
its declaration; with it, only `//!`/`/*!` lines travel, and a definition is
placed under the `\rSec` section its `\ref` group names, so `specgen
generate` renders the same wording from either form.

## Consequences

- The converter shares the front end's plumbing (the compile-argument
  precedence, the parse, the §3.4 body splice, the §3.1 comment anchor) and
  none of its model: it produces no IR and reads no markup beyond the `\ref`
  and `\rSec` structure comments it places by.
- The release archive and the wheel carry two executables, each static
  against Clang, so each is roughly the size of the other; the staging,
  portability and wheel checks run over every executable in `bin/`.
- A header converted with `--move-comments` is pinned by a `.same_wording`
  case: the conversion of a specgen-form header is a wording no-op.
- Members the tool can not move safely (a body spelled through a macro, a
  member inside an `#if` the class is not inside, a member of an unnamed
  class) are refused with a diagnostic and left in the class, never guessed
  at.
