# A paper is the unit of an invocation

## Status

Accepted

## Context

A paper proposing several headers is several specgen documents: the document
tree is built from one main file, and the IR JSON format holds one document.
`render` learned to take several `--from-ir` inputs for issue #109, so that
validation could run across the union of their documented names — a name
specified by a sibling document is not foreign — but two constraints kept a
whole paper from being one command:

- `generate` read one header per run, so the headers had to become IR files
  first, one `--emit-ir` invocation each.
- Several documents *required* `--split`, so a paper's assembled clause — the
  thing an author diffs against `source/utilities.tex` in the working draft —
  could only be had by concatenating the fragments back together outside the
  tool.

Downstream projects filled both gaps with shell. The wording script in
`steve-downey/expected` ran three `generate --emit-ir` invocations into a
temporary directory, one `render --split`, renamed the fragments, dropped the
ones it did not want, and concatenated six files in an order it restated by
hand. The order it restated was, in fact, exactly the order `fragments::split`
had produced them in — a fact nothing checked, and which a change to either
side would have broken silently.

## Decision

Both rendering commands take a paper.

1. `generate` accepts several headers, one document each, in the order named.
   `--root` repeats and pairs with them, as it already does with `--from-ir`.
2. Several documents no longer require `--split`. With `-o` (or standard
   output) they render as one joined whole: each document rendered in turn,
   separated by a blank line.
3. `-o` and `--split` are usable together, and produce the whole and the pieces
   from one parse. The exclusion between them is removed: with the whole
   defined, an output path alongside a split directory has a plain meaning.
4. `--emit-ir` still takes exactly one header, and says so. One document per
   file is what the IR format is; there is no envelope for several.
5. Both commands reach the same two functions — `emit_wording` for one
   document, `emit_paper` for several — so single-pass wording equals two-pass
   wording for a paper exactly as it already did for a document.

The whole and the pieces are two views of one render, and their relationship is
pinned rather than assumed: `golden.paper_whole.is_fragments` joins the split
fragments in manifest order and requires the bytes to equal the `-o` output.

## Consequences

- A paper is one command, one parse, and one validation pass. The IR round trip
  is needed only when the IR itself is wanted.
- `golden.paper.singlepass` extends the `.singlepass` invariant to a paper:
  `generate h1 h2` must produce what `render --from-ir ir1 --from-ir ir2` does.
- Downstream concatenation scripts can be deleted, and the order they restated
  by hand is now specgen's own and checked.
- The `-o`/`--split` exclusion's original rationale — "`--split` writes a set of
  files, so there is nothing for a single output path to mean beside it" — no
  longer holds, because the whole is now a thing the tool can write. `--split`
  *alone* still writes only the pieces: printing the whole to standard output
  beside them would bury the manifest.
- The per-header compile-flag resolution runs once per header rather than once
  per invocation. Papers whose headers share a `--` tail get the identical
  answer each time; a compilation database is entitled to answer differently
  for two files, and now can.
