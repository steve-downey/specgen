# The wording diff is a diff of the IR, carried as marks in the IR

## Status

Accepted

## Context

A paper that modifies an existing facility proposes an edit, with removed
text struck and added text underlined, while the draft PR for the same change
is the new wording, plain. specgen produced the second and nothing toward the
first: `--paper` said "all of this is added", which is true of a new facility
and false of a changed one. Design §7 had reserved the serialized IR as "the
seam for future IR-level diff markup" and left the markup to be written by
hand. The plan that closes this is
[plans/wording-diff.md](../plans/wording-diff.md).

Three levels were available to diff at. Rendered text, as `latexdiff` or an
HTML differ would: backend-specific, blind to `\tcode{...}` and `@...@`
escapes, three differs for three backends. Header source, git's own diff
projected into wording: a docblock reorder is no wording change and a
`static_assert` edit moves a derived paragraph somewhere else. The IR: the
wording ontology, Clang-free, serialized for every document, and tree-shaped,
so matching can be structural where structure exists.

## Decision

1. The diff is computed over `ir::Document`s and its result is an
   `ir::Document` with change marks. There is no second tree type and no
   second render algebra.
2. The marks live in `ir`, because a `::: add` div or the draft's `\added`
   is an editing instruction, semantic to the paper, not typography:
   `Change` (`Same`, `Added`, `Removed`) on every node kind and on a
   description element; `ChangedInline`, a marked run of `PlainInline`s, for
   prose; a `ChangeRun` table beside the span table for code. A mark on a
   node covers everything beneath it, and marks never nest, so a run holds
   the four plain inline kinds as a separate, non-recursive variant.
3. The JSON schema grows additively: every mark's key is omitted at `Same`
   (`changes` when empty), so a document emitted before the marks existed,
   and every unmarked document after, reads and re-emits byte for byte. A
   tagged variant's parse dispatches a key shared by several alternatives to
   the one the tag selects, wherever the tag appears.
4. Each backend renders a mark in its framework's own vocabulary and nothing
   else: mpark's divs and `.add`/`.rm` spans, the draft's `\added`,
   `\removed`, `addedblock` and `removedblock` from `macros.tex`, wg21org's
   `WG21_CHANGE`, special blocks and `insert:`/`delete:` links. A code run is
   wrapped inside the escape the backend has already opened, with the spans
   in it in their escaped form, one line at a time.
5. Paper mode is the empty-baseline diff: every top-level node marked
   `Added`, rendered through the same marks. The three per-backend paper-mode
   wrappers are gone, and the `paper_mode` goldens pin that not a byte
   moved. The LaTeX backend accepts `--paper`, since `addedblock` is the
   thing to emit.
6. Only an added paragraph takes the `x`, `x+1` literal; a retained or
   removed one keeps `#` and its number in the draft. Which paragraphs are
   added is read back off the rendered text, by the divs or blocks the
   backend itself wrote.

## Consequences

- A hand-marked IR renders in all three backends today; the differ that
  produces marks from two documents, and the baseline options that feed it,
  are the plan's later PRs and change nothing here.
- The draft PR is unchanged: the plain render of the current header. The
  paper diff is the same source with marks, so the two cannot disagree.
- Adding an alternative to `Inline` broke every visitor over it, which is the
  tripwire doing its job; the validator looks inside a changed run.
- `render_code_spans` has two forms. The spans-only walk leaves a run's text
  unmarked and is for code the diff never marks; a backend's code blocks use
  the full walk, and a backend that forgot would render a marked synopsis
  silently plain, which the `diff_marks` goldens would show.
