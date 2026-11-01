# A nested namespace is dropped, specified, or foreign

## Status

Accepted

## Context

The front end knew two kinds of namespace (`docs/architecture.md` §3.5). The
*dropped* set is `std` plus the header's own top-level namespaces,
prefix-minimal, which the mapping makes `std`: a qualifier resolving into it
is deleted, and `enclose_in_namespace_std` wrote the one enclosure every
synopsis got. Everything else was *foreign*: its qualifier survived verbatim,
was recorded as `ir::ForeignNamespace`, and §9's leakage checker reported
every `name::` occurrence as an Error. The one exception was a namespace
rooted in `std`, the standard's own vocabulary, which rendered silently.

A nested namespace that is part of the proposed interface is neither.
`beman.transcode` declares its range adaptor object in
`beman::transcoding::views` and, after SG16 review, its WHATWG surface in
`beman::transcoding::whatwg` to mirror the Encoding Standard one to one
([#209](https://github.com/steve-downey/specgen/issues/209)). The generated
wording flattened both into `namespace std {`: the paper proposed
`std::null_term` where it meant `std::views::null_term`, and a
`whatwg::decode<E>` variable template landed beside a `decode(d)` function
template as siblings, which is not the interface and not valid C++. A
`views::x` written in code was an Error, exactly as `detail::x` is, because
nothing told the two apart.

Nothing in the AST does. Whether `views` is interface and `detail` is
implementation is the author's intent, so it needs a marker.

## Decision

A third class, **specified** namespaces, decided entirely in the front end.

A nested namespace below the dropped prefix is specified when some
redeclaration of it, anywhere in the translation unit, carries
`//! \specified` as its docblock, or when its path is rooted in `std`. The
marker is read across the whole parse so a project writes it once, in a header
every other header includes, the reach `\expos` already has into an included
header. A declaration's *rendered namespace* is the longest prefix of its path
past the dropped one every component of which is specified; an unmarked
`detail` flattens into whatever encloses it, as it always flattened into
`std`.

Three consequences, all text, so `ir::Document` does not change and the two
pipelines agree by construction:

- a gathered header synopsis opens a `namespace views { ... }` block for a
  run of declarations in one rendered namespace and closes it when source
  order leaves it ([ranges.syn], [time.syn]); a group header between
  declarations goes with the declaration it heads;
- a class synopsis in a rendered namespace encloses as `namespace std::whatwg
  {` ([fs.class.path]);
- a namespace-scope function's or variable's itemdecl qualifies its
  declarator-id, `proto::decode`, as the draft writes `ranges::copy` and
  `filesystem::absolute`. A type's stays bare, as the grammar requires and the
  draft writes them, and so does a member of a class in such a namespace.

`collect_foreign_namespaces` skips a specified namespace as it skips a
`std`-rooted one, so the leakage checker never sees `views` while an unmarked
`detail::` reports exactly as before. `\specified` off a namespace is an
Error, and so is anything beside it in a namespace's docblock, a namespace
having no wording of its own. A docblock on a namespace that does not carry
the marker is not the namespace's: Clang attaches a detached
`\verbatim-itemdecl` block written above a `namespace` line to it, and
classify already owns that block.

Opt-in. An unmarked header renders byte for byte as before, which the whole
golden corpus pins.

## Rejected

**Opt-out: render every nested namespace unless marked or named `detail`.**
It silently withdraws the leakage Error from every existing user's `detail::`
the day they upgrade, and a name heuristic contradicts
`construct-recognition`: the AST says what a construct is, and here it cannot.

**A command-line or document-level list of rendered namespaces.** Every other
such statement lives in the header, because the header is the specification
source and a flag is not reviewed with it.

**`\expos` on the namespace to mean implementation.** `\expos` already means
exposition-only, an entity the wording shows in italics; a namespace it would
hide is the opposite.

**Qualifying every itemdecl's declarator.** `template<class T> concept
whatwg::C` and `using whatwg::alias_t` are not C++, and the draft writes
concepts, aliases and enumerations bare in sections whose synopsis block
supplies the namespace.

## Consequences

- One new marker, `\specified`, one row in the registry, one field on
  `grammar::Markers`.
- `directive_sets` gains the pre-pass `build_specified_namespace_set` and the
  projections `rendered_namespace` and `declarator_qualifier`;
  `header_synopsis` gains `NamespaceRuns` and `enclose_in_namespace` replaces
  `enclose_in_namespace_std`; `document_build::SynopsisDecl` carries the nested
  namespace its enclosure names.
- A class *defined* inside a nested block of a gathered region is formatted
  for one enclosure and indented for two, so a line of it at the draft's width
  lands two columns past it. The draft's header synopses declare classes and
  define them elsewhere, which is the shape every other path formats exactly.
- The index form is unchanged: `\indexlibraryglobal{decode}`, as the draft
  indexes `ranges::copy`. Whether a `views::` object should index as
  `\indexlibrarymember{name}{views}`, as the draft does for `views::filter`,
  is open and separate, the index being backend metadata that can move
  without touching the text settled here.
