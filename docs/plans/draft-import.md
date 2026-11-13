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
specgen import <draft>/source/utilities.tex --section func.wrap.move --emit-ir > func.wrap.move.json
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

## Open questions

### inline-rendering

How each new inline looks in mpark and org. Recommendation: the table in part 1, italic for
placeholders, terms and old concept names, with the `Cpp17` prefix written out where there is no
macro to supply it.

### import-vocabulary-order

Which kinds land in step 1 and which wait for a paper that needs them. Recommendation:
`PlaceholderInline`, `OldConceptRef`, `RangeInline`, `TableRef` and `RawInline` first; `TermInline`
and `MathInline` when a round-trip golden needs them.

### asdraft-default

Whether an undocumented declaration under `--baseline-draft` inherits without a marker.
Recommendation: no. The coverage invariant (design §9, check 1) means "every declaration has a
markup block", and a marker keeps that true and visible.

### asdraft-spelling

The marker's name. Recommendation: `\asdraft`; `\inherit` reads as class inheritance in a C++
header.

### synopsis-patching

Whether an inherited synopsis is patched from the roster or only checked against it.
Recommendation: patched, with the check as the safety net for an authored one.

### draft-pin

Where a paper records the draft revision it imports from. Recommendation: nowhere in specgen; the
build names a directory, and the paper's makefile owns the checkout, as it owns `--baseline-rev`'s
revision.
