<!-- markdownlint-disable MD013 -->

# Plan: the working draft as the source of unchanged wording

**Status:** PROPOSED, 2026-11-13. Nothing here is implemented. The questions at the end are the taste
calls the user owns; each records the recommendation it is asked with.

No issue yet; this document is the proposal.

## Why

A paper that changes an existing facility has three copies of the wording it does not change:

1. the working draft's LaTeX, which is the authority;
2. the header's docblocks, transcribed from it by hand so that the generated wording reads as the
   draft's;
3. a baseline for the diff (plans/wording-diff.md), which today is another revision of the same
   header: for `beman.constexpr_callables`, the branch `p3574-baseline`, the headers with the
   paper's `constexpr` removed. It is a second transcription, and has to follow every docblock edit.

Deriving the wording from the code instead does not remove copy 2, because the draft's phrasing is
not a function of the code. Measured on `move_only_function.hpp` and `copyable_function.hpp` with
specgen 0.23.0, by deleting every authored *Constraints* and *Mandates* and switching the one-line
*Effects: Equivalent to* elements to `\effects-equiv`: one element of all of them came out as the
draft has it (the *Mandates* of `move_only_function(F&&)`). The rest differ in ways the code cannot
predict:

| The draft writes | The derivation writes |
| --- | --- |
| `VT`, after "Let `VT` be `decay_t<F>`." | `decay_t<F>`, as the `requires`-clause must |
| "`VT` is the same type as `T`" | "`is_same_v<decay_t<T>, T>` is `true`" |
| a bulleted list with ", and", for two conditions as well as three | one sentence up to three |
| "is not a specialization of `in_place_type_t`" | `detail::is_in_place_type<...>`, a leak |
| *Effects*: Equivalent to: `x.swap(*this);` then *Returns*: `*this`. | one code block with `return *this;` |
| `std::move(f)` | `move(f)` |

Each row could be closed with a phrasing rule or a knob. Each would exist to imitate one clause's
habits, and the next clause has others. The draft itself writes "is (not) the same type as" 36 times and
"`is_same_v<...>` is `true`" 76 times in the library clauses.

So the model for a changed facility is the reverse of the model for a new one. For a new facility
the code is the source and the wording is derived. For an existing one the draft is the source, the
code has to *conform* to it, and the tool's job is to check that and to spare the author the typing.
The drift validator (design §9, check 4) already does the checking, in both directions, for
*Constraints* and *Mandates*. What is missing is a way to get the draft's text without retyping it.

What that buys, for a paper such as P3574:

- the baseline is the draft, so "matches the working draft" holds by construction, and the baseline
  branch goes;
- the header carries the code, the section markers, and the wording the paper *changes*;
- the item declarations still come from the code, so the `constexpr` the paper adds is read, not
  typed, and the diff marks it;
- a place where the implementation departs from the draft is a diff mark or a drift warning, not a
  transcription error nobody sees.

## The obstacle: the IR does not hold the draft's inline vocabulary

The importer reads LaTeX and produces `ir::Document`. It never passes LaTeX through: the three
backends render the IR, as they render everything (design §8, "adding wording to one means adding
it to all three"). The difficulty is then what the IR can say, and today it cannot say what a
transcribed docblock already contains. From `move_only_function.hpp`, `--backend mpark`:

```text
[#]{.pnum} ... of the placeholders \cv{}, \placeholder{ref}, and \placeholder{noex} where
[#]{.pnum} *Preconditions*: `VT` meets the \oldconcept{Destructible} requirements, ...
```

and the org backend writes the same. The docblock grammar keeps a command it does not know as prose
(design §4), `Paragraph` has four plain inline kinds (`TextInline`, `CodeInline`, `RefInline`,
`ConceptRef`), and only the LaTeX backend happens to print a `TextInline` holding `\cv{}` correctly.
This is a defect now, in authored wording, and the importer would meet it on every paragraph.

The vocabulary is small where it matters. Across the library clauses of the draft
(`utilities` through `exec`, index lines excluded) there are 237 distinct macros and about 130,000
uses, and the distribution is steep:

| Group | Macros (uses) | Today |
| --- | --- | --- |
| Structure | `\begin`/`\end` (29,010), `\pnum` (12,286), `\item` (4,070), `\rSec` (2,280), element tags (about 9,000) | in the IR |
| Code and names | `\tcode` (37,105), `\exposid` (10,442 with `nc`), `\libconcept` (5,303), `\exposconcept` (2,916), `\seebelow`, `\expos` | in the IR |
| References | `\ref` (2,149), `\iref` (1,884), `\tref` (202) | `\tref` missing |
| Italic terms | `\placeholder` (1,594 with `nc`), `\oldconcept` (619), `\cv` (142), `\term` (139), `\defn` family (253), `\grammarterm` (237) | prose only |
| Code-like | `\range` family (about 640), `\keyword` (236), `\libmacro` (128), `\libheader` family (113), `\unspec` (413 with `nc`) | prose only |
| Fixed phrases | `\impldef`, `\impdefx` (223) | span only |
| Layout hints | `\brk`, `\br`, `\linebreak`, `\newline`, `\-`, the `nc` variants | to be dropped |
| Mathematics | `\bigoh` (150), `\frac`, `\leq`, `\cdot`, `\log` and the tail | missing |
| Tables | `lib2dtab2`, `libtab2` in the IR; `\rowsep`, `\lhdr`/`\chdr`/`\rhdr`, `\capsep`, `\topline` of the other forms | partly |

## Design

Four parts. Each stands on its own and each later one needs the earlier.

### 1. Inline kinds

`PlainInline` grows by a small closed set. The IR records what the text *is*, not how the draft
typesets it, so that mpark and org choose their own form:

| Inline | From | LaTeX | mpark | org |
| --- | --- | --- | --- | --- |
| `PlaceholderInline{text}` | `\placeholder`, `\placeholdernc`, `\cv{}`, `\cvqual` | `\placeholder{x}`; `\cv{}` for `cv` | `*x*` | `/x/` |
| `OldConceptRef{name}` | `\oldconcept{X}` | `\oldconcept{X}` | `*Cpp17X*` | `/Cpp17X/` |
| `TermInline{text, role}` | `\term`, `\defn`, `\defnx`, `\defnadj`, `\grammarterm` | the macro for the role | `*x*` | `/x/` |
| `RangeInline{kind, first, last}` | `\range`, `\crange`, `\orange`, `\brange` | the macro | `` `[first, last)` `` | `~[first, last)~` |
| `TableRef{stable_name}` | `\tref` | `\tref{x}` | a link to the caption anchor | an org link |
| `MathInline{tex}` | `$...$`, `\bigoh{...}` | as written | `$...$` | `\(...\)` |
| `RawInline{latex}` | anything else | as written | pandoc raw `` `...`{=latex} `` | `@@latex:...@@` |

`\keyword{x}`, `\libmacro{x}`, `\libheader{x}` and `\libheaderref{x}` become a `CodeInline` (`x`,
`x`, `<x>`), with a `LibraryIndex` span where the draft indexes them. `\unspec` and the `\impldef`
family become the `SeeBelow`-style span kinds a `CodeText` already has (`ImplDefined`, and a new
`Unspecified`), usable in prose through a `CodeInline`. `\opt{x}` inside code is a new span kind,
`Optional`.

A `RawInline` is a Warning when the importer produces one, naming the macro and the subclause, and
it is the only place LaTeX survives into a non-LaTeX backend. A paper imports the subclauses it
changes, so its warning list is short and says which kind to add next. Nothing degrades silently.

The docblock grammar reads the same macros into the same kinds, so authored wording gets the fix
too, and the leakage, drift and reference checks see through them where they see a `TextInline`
today. A `PlaceholderInline` inside backticks is the `Placeholder` span that already exists.

JSON grows additively under a new item tag, the way `\covers` did (decision
explicit-constraint-coverage): an older reader rejects an item it cannot represent.

### 2. `specgen import`

```sh
specgen import <draft>/source/utilities.tex --section func.wrap.move -o func.wrap.move.json
```

One or more `--section` stable names; the result is one `ir::Document` whose top-level sections are
those subclauses, rebased the way `--base-section-depth` already measures. Clang-free (decision
clang-free-render-mode): a Tier-A component beside `diff`, built on the combinator parser the
docblock grammar uses (decision parser-combinators).

What it reads is what the LaTeX backend writes, plus the draft's own habits that the backend does
not produce:

- `\rSecN[name]{title}`, `\pnum`, `itemdecl`, `itemdescr`, `codeblock`, `codeblocktu`, `itemize`,
  `enumerate`, `note`, `example`, the element macros, the two library table environments;
- `\indexlibrary...` lines before an `itemdecl`, kept as the item's index entries; every other
  `\index...` is dropped;
- one `itemdecl` holding several declarations is one item with several signatures, split at the
  declaration boundary so that the differ's item matching ("by what they declare") works;
- a synopsis is a `codeblock` directly under a section, kept as an anonymous `Synopsis` whose
  `name` is the class it declares when it declares exactly one;
- `@...@` escapes in code become spans; an escape the span vocabulary lacks stays an authored
  escape, as in a `\verbatim-itemdecl` payload today;
- source line breaks inside a paragraph are not content.

Its test is a round trip that needs no header: import a subclause, render it with `--backend latex`,
and compare with the draft's source after normalizing line breaks, the layout hints, and the `nc`
variants. The corpus stays hermetic (decision hermetic-corpus): the goldens are draft slices copied
into `tests/corpus/draft/`, with their revision recorded, not a checkout.

Then `--baseline-ir` takes the import as it takes any saved IR, and one flag does both steps:

```sh
specgen generate hdr.hpp --baseline-draft <draft>/source --backend mpark -- -Iinclude
```

`--baseline-draft` imports, from the named source directory, exactly the top-level sections the
current document generates, by stable name. A section the draft does not have is new wording and
renders as all added, with a note, as a header missing at `--baseline-rev` does.

### 3. Inherited descriptions

A declaration in a header may take its description from the baseline instead of a docblock:

```cpp
//! \asdraft
constexpr move_only_function(move_only_function&& f) noexcept;
```

The item's description is the description of the baseline item the differ pairs it with. The
pairing is the differ's and is made on the *unmarked* signature, so adding `constexpr` does not
break it; an item with no pair is an Error. Authored elements beside `\asdraft` replace the
inherited element of the same kind, which is how a paper changes one paragraph of an item and
leaves the others.

Inherited *Constraints* and *Mandates* are authored wording as far as the validator is concerned:
the suppressed derivation's conjuncts are checked against them in both directions (design §9,
check 4), `Let` bindings and same-type prose included. `\covers` rides along on the marker for a
predicate prose cannot prove:

```cpp
//! \asdraft
//! \covers constraints !detail::is_in_place_type<remove_cvref_t<F>>
```

The same applies one level up. A class whose docblock is `\asdraft` takes its description and, if
it asks for it, its synopsis from the baseline; a section's detached prose is inherited when the
section has none of its own.

### 4. A synopsis that agrees with the code

A `\verbatim-synopsis`, authored or inherited, is exact text, and nothing ties it to the
declarations it stands for. For a paper whose whole change is a specifier, that is the one line
that matters. A new validation check pairs each roster entry with the synopsis line that declares
it (by name and parameter types, placeholders compared as wildcards) and warns when the two
disagree on `constexpr`, `explicit`, `noexcept`, `static`, `friend`, or `= default` / `= delete`,
or when either side has a declaration the other lacks.

For an inherited synopsis the check is not enough, because the text is the baseline's. The
synopsis is *patched*: for each paired line whose specifiers differ from the roster's, the roster's
are written, which is the change the differ then marks.

### Independent of the above

Two derivation changes are worth making whatever is decided here, because new wording wants them:

- `\effects-equiv` together with `\returns-expr`: the body's final `return E;` becomes *Returns*,
  the statements before it *Effects: Equivalent to*. A one-statement equivalent renders inline, as
  the draft writes `Equivalent to: \tcode{x.swap(*this);}`.
- Extracted code keeps `std::` on `move` and `forward`, as the draft does.

Found on the way: an extracted body containing `std::move(f)` reports `move` as a foreign name
when an included header declares an enumerator `move` (`manage_op::move` in callables'
`detail/call_base.hpp`). Design §9 says a standard-qualified occurrence is not a bare use; the
check runs after the qualifier is dropped. That is a bug and gets its own issue.

## What it does not do

- It does not import the core language clauses. Grammar productions, `bnf` environments and
  footnotes are out of scope until a paper needs one.
- It does not track the draft. A paper names a draft revision; rebasing onto a newer one is a
  deliberate act, and the diff shows what moved.
- It does not make placeholders such as `cv`, `ref` and `noex` extractable. A class that stands for
  a family of specializations keeps an authored or inherited synopsis; part 4 is what checks it.

## Steps

Each is a PR that ends green.

- **Step 1.** IR: the inline kinds, the span kinds, JSON under a new item tag with the legacy tags
  accepted. Backends: every kind in all three. A render-mode golden from hand-written IR, one
  expected file per backend. Every existing golden unchanged.
- **Step 2.** Docblock grammar: the macros of part 1 lower to the kinds. Goldens that carry a
  transcribed `\placeholder`, `\cv{}` or `\oldconcept` move in mpark and org, and only there.
  Validator: the checks that read `TextInline` and `CodeInline` read the new kinds.
- **Step 3.** `import`: the parser, `--section`, `--emit-ir`, the `RawInline` warning. Round-trip
  goldens over `tests/corpus/draft/` slices: [func.wrap.move], [optional.ctor], and one clause with
  a table.
- **Step 4.** `--baseline-draft` on `generate`; a diff golden with a corpus header against a draft
  slice. An `examples/cli/` script and capture, a user-guide section.
- **Step 5.** `\asdraft` on a member, with element replacement and `\covers`; the unpaired Error;
  drift goldens.
- **Step 6.** `\asdraft` on a class and on section prose; the synopsis check; synopsis patching.
- **Step 7.** `beman.constexpr_callables`: the three headers move to `\asdraft`, the baseline
  branch and tag are retired, and `make wording-diff` uses `--baseline-draft`. The acceptance test
  is that `papers/P3574/wording/diff/` still has exactly the `constexpr` additions.

The two independent derivation changes and the `std::move` bug are separate PRs at any point.

### Where each step landed

Steps 1 to 3 are [#273](https://github.com/steve-downey/specgen/pull/273),
[#274](https://github.com/steve-downey/specgen/pull/274) and
[#275](https://github.com/steve-downey/specgen/pull/275). Steps 4 and 5 are two commits of
[#276](https://github.com/steve-downey/specgen/pull/276), and step 6 is
[#277](https://github.com/steve-downey/specgen/pull/277), which grew the changes in
[what the real headers needed](#implementation-log). Step 7 is the draft PR
[constexpr_callables#9](https://github.com/steve-downey/constexpr_callables/pull/9), which needs
a specgen release first. Its acceptance test holds: the wording diff has the same marks as the
baseline-revision diff did (30, 34 and 2 in the three clauses), and the plain wording is
unchanged for [func.wrap.move] and [func.wrap.copy].

Not done, and left for the owner: deleting the `p3574-baseline` branch and the
`p3574-baseline-draft` tag in `beman.constexpr_callables`. The build no longer reads them.

## Open questions

### inline-rendering

**Status:** decided, 2026-11-13. **Decided by:** the project owner.

How each new inline looks in mpark and org. mpark and wg21org are not semantic markup the way
wg21-latex is, so a new inline is rendered to look like what the draft's LaTeX prints, in the
target's own emphasis and code forms: placeholders, `cv`, terms and old concept names are italic
(`*x*`, `/x/`), with `Cpp17` written out where the LaTeX macro supplies it; ranges, keywords,
library macros and headers are code. The table in part 1 is the first cut under that rule, and the
rule, not the table, decides a case it does not list.

### import-vocabulary-order

**Status:** decided, 2026-11-13. **Decided by:** the project owner.

Which kinds land first and which wait for a paper that needs them. `PlaceholderInline` and
`OldConceptRef` shipped with step 1. `RangeInline`, `TableRef` and `RawInline` come next.
`TermInline` and `MathInline` wait until a round-trip golden needs them.

### asdraft-default

**Status:** decided, 2026-11-13. **Decided by:** the project owner.

An undocumented declaration under `--baseline-draft` does not inherit without a marker. The
coverage invariant (design §9, check 1) means "every declaration has a markup block", and a marker
keeps that true and visible.

### asdraft-spelling

**Status:** decided, 2026-11-13. **Decided by:** the project owner.

The marker is `\asdraft`. `\inherit` reads as class inheritance in a C++ header.

### synopsis-patching

**Status:** decided, 2026-11-13. **Decided by:** the project owner.

An inherited synopsis is patched from the roster. The check stays as the safety net for an
authored synopsis.

### draft-pin

**Status:** decided, 2026-11-13. **Decided by:** the project owner.

Specgen records no draft revision. The build names a directory, and the paper's makefile owns the
checkout, as it owns `--baseline-rev`'s revision.

## Implementation log

What the plan did not foresee, found by running the implementation over the real headers. Each
was decided while implementing, **Decided by:** the implementer, and is open to review. They are
named for the question, as the open questions above are.

### inherit-without-diff

The plan had `--baseline-draft` only. A paper also wants its own wording, which needs the draft to
resolve `\asdraft` and must render with no marks. `generate --inherit-draft <dir>` is that: the same
directory, the same resolution, no diff. Giving both is an error.

### entity-names-for-pairing

The differ pairs items by the names they declare. The importer takes them from `\indexlibrary`
entries, and the front end now names a constructor or destructor by its class without the template
arguments clang spells it with (`box<T>` becomes `box`). Fourteen goldens carry that one-field
change. Overloads of one name pair by how alike their signatures read, best first, in the differ and
in `\asdraft`.

### nameless-items

A detached `\verbatim-itemdecl` names no entity, and neither does the draft's index for an
exposition-only variable. Such an item pairs with the other side's item that reads most like it,
if it reads alike at all, both for `\asdraft` and in the differ. A name known on both sides that
differs is still a rename, an item removed and an item added.

### same-wording-same-ir

Authored text and imported text of the same wording had different IR, so the differ showed
retyped wording as changed. The differ now reads authored listings (`@\cv{}@` escapes) the way the
importer does, reads a typed `Equivalent to:` and its code block as the `EquivalentTo` block, names a
typed synopsis for the class it declares, and diffs deleted and inserted lines that read as nearly
the same code over a different number of lines as one run of tokens, so a line break is not a
change. Generation is not touched: a `\verbatim-itemdecl` stays exact, span-free text.

### synopsis-source

`\asdraft-synopsis` patches the draft's synopsis from the declarations the class makes, kept
beside the text in `Synopsis::declared`. A member the class inherits from a base is not among them,
so `move_only_function` and `copyable_function`, whose `operator()` comes from `detail::call_base`,
keep an authored `\verbatim-synopsis`. The check compares that with the class's declarations, and
reports a declaration the synopsis has and the class does not as a note, not a warning. Open: a way
to say that a declaration the class does not make is made elsewhere, so the synopsis can still be
inherited.

### importer-vocabulary-gaps

`\ref`, `\cvqual`, `\exposconcept`, `\newline` and `\opt` have no IR kind. The importer keeps
each as raw LaTeX and warns, and keeps a whole `\tcode` holding one as raw LaTeX, since the macro as
text would be escaped. They occur only in [func.wrap.ref], where the headers author the wording, so
`beman.constexpr_callables` lets exactly those warnings through. Open: the kinds.

Log:

- 2026-11-16: `\ref`, `\exposconcept` and `\cvqual` in prose are read (issue #307). None needed
  an inline kind of its own: each is a flag on the kind its nearest relative imports to
  (`RefInline::bare`, `ConceptRef::exposition`, `PlaceholderInline::cv_qualifier`), so every
  consumer that dispatches on the inline's kind is untouched and the differ still sees one
  token. [func.wrap.ref] is now a corpus slice, with a golden for what its import reports.
- 2026-11-16: `\cvqual` and `\opt` inside code are read (issue #307), as two span kinds,
  `CvQualifier` and `Optional`. A flag would not do here: a `Span` has a kind and a payload and
  nothing else, and the kind is what a backend dispatches on. `Optional` covers the code that
  may be absent, so its argument has to be code alone; an `\opt` around another macro is still
  kept raw, because spans do not nest.
- 2026-11-16: the no-italic-correction forms survive the import (issue #306). The plan had them
  dropped, with the layout hints, and the round-trip test normalized them away; the consumer's
  claim is that unchanged wording is the draft's text, so they are kept. They are typesetting,
  so they sit beside the wording and not in it: `Span::no_correction`, three-state, and a flag
  on the two prose inlines. Unset means "what the context takes", and the importer sets it only
  where the draft wrote the other form, so a generated span and an imported span that the
  generator would have written the same way are the same IR. The differ compares through
  `without_typesetting`. Measured on the draft's library clauses: `@\exposid{...}@` in a
  listing 6,351 times against `@\exposidnc{...}@` 475, so the listing default this tool has
  always written is the draft's minority form; that default is not changed here.
- 2026-11-16: the line-break hints inside code survive too (issue #306), which also reads the
  last of the five macros, the `\newline` in a `\tcode` (issue #307): [func.wrap.ref] imports
  with no warning. They are `CodeText::hints`, a table of offsets beside the span table. Three
  other shapes were weighed. Leaving the macro in a span's payload is what happened before for
  `\exposid{bound-en\-tity}`, and it put LaTeX in the mpark and org output and made the name
  differ from the same name written plainly. Zero-width spans cannot sit inside another span.
  U+00AD and U+200B in the text are the right characters for `\-` and `\brk`, and would have
  covered prose as well, but they are invisible in a golden and every comparison of code text
  would have had to remember to strip them. Measured on the library clauses: about 560 hints
  inside a `\tcode` and 17 inside an `\exposid`. Open: the hints in running text (`\br` 180,
  `\-` 93, `\newline` 42, `\brk` 28, `\linebreak` 23), which are still dropped, and the ones
  in the argument of a prose inline such as `\oldconcept{Unary\-Type\-Trait}`, which are still
  left in the name.
- 2026-11-17: mathematics inside a `\tcode` is read where it is a variable or a subscript
  (issue #311): `get<$i$>(u)` and `T$_i$`. As code text the dollar signs were escaped by the
  LaTeX backend and printed. They are two span kinds, `MathVariable` and `Subscript`, for the
  reason `\cvqual` and `\opt` are. The text of a subscript span is the subscript after an
  underscore, so the code reads `T_i` to everything that reads the text alone. Any other
  mathematics in a `\tcode` keeps the whole `\tcode` as raw LaTeX, with a warning, which is
  what a macro with no kind does. Measured on the library clauses: 691 `\tcode`s hold
  mathematics, and 487 of them hold only these two shapes. In the other 204 the shapes are a
  subscript on code written inside the mathematics (`$\tcode{T}_i$`, 132), an ellipsis
  (`$\dotsc$`, 43), a subscripted variable (`$r_K$`, 24), and 59 others. Open: those.

### prose-line-joins

How the lines of a paragraph join. A line break is a space, and a `%` takes the rest of its line
and the line break with it. The importer had every `%` join its line to the next with no space,
which is right for `text%` and wrong for `text % note`, where the space before the `%` is still a
space: [tuple.cnstr] has `is\linebreak{} % Overfull` before a `\tcode`, and the two ran together
(issue #313). The space before the comment now decides. A line that is only a comment says
nothing either way, where before it joined the lines around it.

### paragraph-after-a-block

Which paragraph the prose after a list, a code block or a table belongs to. A backend numbers
the first block of a body, whatever it is, and a `BodyParagraph` says whether it continues the
paragraph before it. The importer set that from whether prose had been read since the last
`\pnum`, so when the block came first, directly after the element's tag, the prose after it was
read as a new numbered paragraph: [optional.specalg]'s *Constraints* for `swap` is a code block
and then "is `true`.", and it came back with two numbers (issue #315). The first block of a body
now takes the pending number, whatever kind it is. A block that is not first in its body leaves
the number pending, because the IR has no way to say that a block which is not prose opens a
numbered paragraph in the middle of a body. Open: that. The draft numbers a note or an example
by itself often (502 times in the library clauses, 255 of them inside an `itemdescr`), and such
a note loses its number unless it is first in its body (issue #321).
