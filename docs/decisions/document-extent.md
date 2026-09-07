# What a document reaches

## Status

Accepted

## Context

A document was one file. The front end processed declarations whose location is
in the main file and nothing else, which made the rule easy to state and easy to
predict: what is in the file is what is in the synopsis.

It also decided how a library must be laid out to be specified. The draft's unit
is the header — `[transcode.syn]` lists everything `<transcode>` declares — so
one document per proposed standard header meant one *file* per proposed header.
A library whose `<transcode>` is an umbrella of sixteen component headers had to
merge them into one 3,500-line file to be specified: a real change to a library,
made for a documentation tool, and the same shape as the `detail::` question
that [#36](https://github.com/steve-downey/specgen/issues/36) turned around. The
answer there was that the tool renders what the library writes, and it applies
here.

The header that stands for a proposed header already exists in such a library,
and already lists its parts. They are its `#include`s.

## Decision

**A document is the main file plus the headers `#include`d inside a gathered
`.syn` region.**

The region is the author's statement of which includes are the specification
surface. An include outside it — `detail/`, a standard header, anything else —
is invisible, exactly as everything reached through an include was when a
document was one file. There is no new marker and no path list on the command
line: the two questions a follow-the-includes rule has to answer are answered by
where the include sits.

Positions are **document offsets**: the position in a virtual concatenation
where each followed include is replaced by that file's contents. Every rule that
orders or compares positions — region extents, the consumed-range watermark,
placement keys, `\also` adjacency — keeps working against them unchanged, which
is why the mapping is this one and not a synthetic counter.

Three rules follow from a document holding more than declarations:

- **A synopsis lists an entity once.** An out-of-line definition folded in adds
  no second entry for a member its class already declares.
- **An out-of-line member definition routes by its class's `\ref` group.** In
  the house style the definition carries the wording, and its group is written
  in the class body beside the declaration; the standing namespace-scope group
  header is not it.
- **A finding names the file it is in.** A line number in the virtual
  concatenation is a line in no file anyone can open.

Extraction is unaffected. It works inside one declaration, in that
declaration's own file, at that file's own offsets — only ordering and the
document's extent are in document offsets.

## Consequences

- A library keeps its per-component headers and is specified from the umbrella
  it already has. Nothing moves to suit the tool.
- A followed header carries no `\rSec` markers of its own: its clauses are the
  umbrella's, after the fence, and its declarations reach them by routing. That
  is the same routing a folded-in class's members
  ([#34](https://github.com/steve-downey/specgen/issues/34)) and a namespace
  entity ([#69](https://github.com/steve-downey/specgen/issues/69)) already use.
- A file first included from somewhere other than the region keeps that first
  inclusion and is not followed. The synopsis lists its parts in an order that
  works, and one that does not is a header the author reorders.
- A document without a `.syn` region is unchanged: the main file, and nothing
  else.
