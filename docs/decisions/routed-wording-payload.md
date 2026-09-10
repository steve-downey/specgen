# What a routed piece of wording is

## Status

Accepted

## Context

Routing is one channel. `document_build::PendingItem` names a stable name, a
placement key, and a piece of wording; `build_tree` stashes each by stable
name and injects it when the matching `\rSec` frame closes. An in-class member
([#34](https://github.com/steve-downey/specgen/issues/34)) and a folded-in
namespace entity ([#69](https://github.com/steve-downey/specgen/issues/69))
both reach their clause that way, and design §9's dangling-route rule reads
the `Routed` roster entry each of them leaves behind.

The channel carried an `ir::SpecItem`, because everything that had ever asked
to be routed was one.

A class's own wording is not one thing. It is two sibling nodes: the
class-scope `static_assert` paragraph of design §5.2, an `ir::FreeParagraph`,
and the class's own description of design §6
([#18](https://github.com/steve-downey/specgen/issues/18)), a
description-only `ir::SpecItem` — an itemdescr with no itemdecl. They stand
together, in that order, immediately after the class synopsis.
`SynopsisDecl` has one field for each, and
[#41](https://github.com/steve-downey/specgen/issues/41) gave them a way out
of a gathered `.syn` region: their own event, pushed beside the region's
node, so a folded-in class's wording stands next to the synopsis it belongs
to rather than being dropped.

Next to the synopsis is the right default and the wrong *only* answer.
[#98](https://github.com/steve-downey/specgen/issues/98) is what that cost: a
library folding its component headers into an umbrella's region
([document-extent](document-extent.md)) and moving each followed class's
`\rSec` markers into the umbrella has no way to send the classes' own
descriptions after them. `\at` on a class definition was read by nothing —
not in a region and not outside one — so the wording stayed in the header
synopsis's section and the named section came out empty, with no finding at
any severity. Removing the marker changed nothing either way.

## Decision

**A routed item is a node, not an item.** `PendingItem::item` is an
`ir::Node`, and `\at` on a class definition routes both of the class's own
nodes through the channel its members already use.

Widening the payload is what lets the routing live in *one* place. The class's
own wording is attached in `classify()`, which is also where its members'
routes are decided, so `route_class_description` runs there and the gathered
`.syn` fold needs no case of its own: a routed class arrives at the fold with
both fields already empty and its wording already in `pending`, which the fold
forwards to the gathered node along with the members'. The same code therefore
routes a class inside a region and one outside it, and "a folded-in class
routes the way a class outside a region already renders" is true because it is
the same path, not because two paths were made to agree.

Both nodes are keyed by the class's own offset, so they arrive in the target
section in the order they would have stood in beside the synopsis — paragraph
first, then description — and ahead of any member of that class routed to the
same section, whose key is further into the file.

The route earns a `Routed` roster entry naming the class in both `name` and
`parent`. That entry is the whole of the diagnostic half: `build_tree` drops a
pending item whose stable name no `\rSec` opens, so the entry is the only
record that the request was made, and design §9 reads exactly it. `parent` is
what lets the finding say which class it is about when the roster is a
gathered node's, which names none of them
([#45](https://github.com/steve-downey/specgen/issues/45)).

An `\at` on a class with no wording of its own routes nothing and is not
reported. Nothing is being ignored there: there is no description to lose.

## Rejected: a second field beside the item

`PendingItem` could have grown an `optional<ir::FreeParagraph>` next to its
`SpecItem`, leaving the payload type alone.

It states the wrong thing. The paragraph is not a part of the item, or a
qualifier on it — it is a sibling node with its own placement key, which is
why `build_tree` pushes it as its own `GroupCandidate`. A second field makes
one pending entry mean two children whenever the optional is engaged, and
every consumer then has to know that and get the order right. The variant
`ir::Node` already exists and already means "a child of a section"; the
channel routing children to sections is the natural place to spell it.

The cost is that alias grouping, the one consumer that reaches *into* the
payload to merge a follower's itemdecl into its primary's, asks for the
alternative back. It already knows: `is_alias` is set only where a marked
alias's `SpecItem` was pushed, and a class's own wording is never an alias.

## Rejected: leave the paragraph behind and route only the description

Routing what fits and reporting the rest would have kept the payload
unchanged. It splits one class's wording across two sections — a derived
"A program that instantiates `C` is ill-formed unless ..." paragraph in the
header synopsis and the `\remarks` it belongs with three clauses later — which
is worse output than not routing at all, and it makes the marker mean
something different for a class than it means for every other entity that
takes it.

## Consequences

- A multi-header library can drop a followed header's own `\rSec` markers the
  way [document-extent](document-extent.md) describes even when its classes
  carry general docblocks: `\at` sends each class's own wording to the
  umbrella's clause for it.
- A misspelled `\at` on a class is a design §9 Error naming the class and the
  section it asked for, in a document where it used to be a silent no-op.
  `tests/corpus/spec_gathered_class_at.hpp` keeps one on purpose and
  `gathered_class_at_validate` pins it.
- `PendingItem` can carry a `Section` or a `Synopsis` and nothing stops it.
  Nothing produces one, and the alternatives are checked where they are read
  rather than at the push, which is the ordinary cost of a closed variant used
  as a payload.
- The class's own description is still an `ir::SpecItem` with no signatures,
  so it goes through the span, table, leakage and drift checks unchanged
  wherever it lands (design §6, §9's empty-item check included).
