# Dependency fragment emission

## Status

Accepted

## Context

Generated wording is a build artifact of the headers it was extracted from, but
nothing said so to a build system. A paper repository ran specgen from a
`.PHONY` target with no prerequisites, or from a script someone remembered to
run, and a PDF built from wording three commits stale looked exactly like a PDF
built from current wording.

The information needed to close that gap exists at the moment of the parse and
nowhere else afterwards. Clang's `SourceManager` knows every file the
preprocessor opened; an `ir::Document` is not a record of where its
declarations came from, and by the time the driver holds one the parse is over.
Nor is the *document's* own file set the right answer: `DocumentFiles::discover`
finds the includes inside a `.syn` region, because those are the document, but
a typedef or a default argument in an implementation header the document never
renders can still change what the derived wording says.

## Decision

`--depfile <path>` writes a Makefile dependency fragment, in the shape a
compiler's `-MD -MP` writes: one rule naming every target, then one empty rule
per prerequisite.

- **Prerequisites** are every non-system file the parses read — `-MMD`
  semantics, not `-MD`. The set over-approximates what could change the wording,
  deliberately: a dependency edge too many costs a spurious rebuild, one too few
  costs a stale paper. System headers are left out because a `.d` naming every
  libstdc++ path buries the three headers a reader is scanning for, and because
  those paths describe the box that ran the tool rather than the paper.
- **Targets** are the files the run actually wrote — `-o`, or every `--split`
  fragment, or both. `--dep-target <name>` overrides them, repeatably, for a
  build whose real target is a stamp file or an installed copy. Writing to
  standard output with neither is a usage error rather than a guess.
- The **`-MP` block is unconditional**. Without it, deleting or renaming a
  header makes every later build fail on a prerequisite the stale fragment still
  remembers, which has to be repaired by hand rather than by rebuilding.
- The fragment is written **after** every output it names. One promising files
  that were never written is worse than none: make believes the next build has
  nothing to do.
- Paths are emitted as the parse resolved them, normalized lexically but no
  further. `./a.hpp` and `a.hpp` are one file and must deduplicate as one — a
  header named on the command line is routinely `#include`d by a sibling too —
  but resolving to `weakly_canonical` would make them absolute and follow
  symlinks, and a fragment is read from the directory the build runs in, where
  the relative spelling is the one that works.
- specgen's own path is **not** emitted. Wording moves when the tool moves, and
  a build that cares can add `$(shell command -v specgen)` as a prerequisite in
  one line; baking a machine-specific binary path into every fragment to say so
  is the wrong trade.

The front end answers "which files" through `build_document_with_sources`, a
sibling of `build_document` rather than a wider return type for it: the older
entry point has sixty-odd call sites that want the document alone, and
`document_build::BuildResult` is `build_tree()`'s return — Tier A, clang-free,
and in no position to know what a file is. The formatting — make's escaping, the
line continuations, the `-MP` block — is `depfile.hpp`, Tier A and unit-tested,
because it is the only part that is neither the front end's answer nor the
driver's.

## Consequences

- A paper repository can make its wording a real file target with real
  prerequisites, and `make` rebuilds it when a docblock changes and does nothing
  when nothing changed.
- `render --from-ir` emits fragments too, naming the IR files it read. A
  two-stage build therefore has both edges and make chains them, without either
  stage knowing about the other.
- A `--depfile` golden cannot be compared like an ordinary one: a fragment is
  nothing but paths. `MODE depfile` runs from the header's own directory with a
  fixed `--dep-target`, the way `MODE diagnose` and `MODE split` already avoid
  absolute paths (§10).
- The prerequisite set is wider than the rendered document, so editing an
  implementation header that changes no wording still triggers a rebuild that
  regenerates identical bytes. That is the intended direction of the error.
