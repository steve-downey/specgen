# Where a top-level section starts is one option per unit

## Status

Accepted

## Context

Every backend has a field saying where a top-level `ir::Section` sits:
`latex::Options::base_section_depth` (3, the draft's library split
granularity), `mpark::Options::base_heading_level` (2, the level the
mpark/wg21 wording examples write at) and `org::Options::base_heading_level`
(2, so a fragment's sections sit under the including paper's `* Wording`
heading). All three defaults were reachable only by a caller constructing an
`Options` in C++; the driver never set any of them, so `render` and `generate`
always emitted at the struct default (issue #97). A paper writing its own
sections at `##` got its generated clauses at `##` as well — siblings of
"Abstract" rather than children of the "Wording" heading that introduces them,
with two dozen clauses listed in the table of contents as peers of the paper's
own eight sections.

Exposing this on the command line raises the question the headers had already
answered for themselves: the LaTeX field and the other two do not hold the same
number. `mpark.hpp` says its field is "the counterpart of
`latex::Options::base_section_depth` and **not the same number**"; `org.hpp`
says it is "the counterpart of mpark's `base_heading_level` rather than of
LaTeX's `base_section_depth` (`\rSec3` is a *draft* split granularity; a
paper's wording starts a level or two below its title)". A `\rSec` depth is a
fact about the working draft's structure; a markdown or org base is a fact
about the paper the fragment is being transcluded into.

## Decision

**One option per unit, not one option per concept.** The driver exposes

- `--base-heading-level <n>` → `mpark::Options::base_heading_level` and
  `org::Options::base_heading_level`, and
- `--base-section-depth <n>` → `latex::Options::base_section_depth`,

and each is a **usage error** on the backends that do not measure in its unit,
in the same way `--paper` and `--new-root` are errors outside mpark: a flag
that quietly does nothing is wording an author believes was produced
differently. Each message names the option that does apply, so the diagnostic
teaches the distinction rather than merely enforcing it.

A single `--base-level` was considered and rejected. It would have to mean 3 to
one backend and 2 to the others for the same document, which is not a default
one flag can carry, and it would erase in the interface the difference the two
headers spend a paragraph each preserving.

The driver holds each value as an unset `std::optional<int>` and reads the
default off a default-constructed `Options` at the call. What an omitted flag
means is the backend's business, and a number restated in the driver is a
second place for it to drift from the header that documents it.

Range checking splits along the same seam. Below 1 is a usage error for both
options, wherever it appears: a top-level section has to *be* a heading and
there is no zeroth one. The upper end belongs to the backend — markdown stops
at six heading levels, org does not — so `--base-heading-level` above 6 is a
usage error under `mpark` and is accepted under `org`. The mpark backend's
existing saturation at six stays exactly where it is, because it is a rule
about *descent*: how deep a document nests is the document's business, while a
base is the one level the author chose, and a chosen level that silently did
nothing is worth saying out loud.

## Consequences

- `--split` needs nothing of its own. A fragment *is* a document
  (`docs/architecture.md` §8), and both paths render through the driver's one
  `render_document` lambda, so a fragment starts at the same base a whole
  render does — a base cannot come to mean one thing in a document and another
  in a piece of one.
- The two options are the driver's only integer-valued ones, so
  `parse_level` in `tools/specgen/main.cpp` is where a number the user typed
  becomes a diagnostic naming the option they typed. `std::from_chars` over
  the whole argument, for the reason [parser-combinators](parser-combinators.md)
  gives: never an exception, and `3x` is a typo rather than a 3.
- `tests/golden/base_level/` renders one three-deep fixture through all three
  backends off the command line. Unit tests over an `Options` value cannot
  catch this bug at all — the fields worked; nothing set them — so the
  end-to-end case is the regression test, and the per-backend unit tests beside
  it pin what the base does to *nesting*.
- Adding a fourth backend adds a third unit only if it genuinely measures in
  one. Sharing a spelling with mpark and org is the right answer for anything
  that counts headings; a new spelling is the right answer for anything that
  does not.
