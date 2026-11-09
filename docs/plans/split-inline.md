# Plan: `split_inline`, moving in-class definitions out of line

**Status:** IN PROGRESS, 2026-11-09. Stages 1 to 3 are on the branch: `tools/split_inline/` builds the executable
from this tree against `beman::specgen.frontend`, both comment modes are implemented as designed below, and
`tests/split_inline/` holds the cases, including the three corpus headers whose wording must not change. Stage 4
is done but for an `examples/cli` capture, which needs a full build to record: the user guide, the inventory, the
release scripts and the wheel carry the second executable, and the decision is recorded in
[decisions/split-inline-in-place-namespace.md](../decisions/split-inline-in-place-namespace.md). Two questions
were settled with the user on 2026-11-08 and are recorded in place: the tool is `split_inline`, a separate
executable rather than a `specgen` subcommand, so that specgen's own comment handling and the converter's never
run in one process. The remaining questions at the end were executed on their recommendations and stay open to be
overridden. A spike against the distribution's Clang 18 (not the pinned 23) checked every source-location claim
below before the tool was written; the numbers and
the excerpt are from that run.

specgen's input contract (design §1) is a declaration-only class body with the definitions following it, out of
line. A header written the ordinary way defines its members where it declares them. Restructuring one by hand is
mechanical and has two classic failure modes: the definitions go into a *reopened* namespace, and a free function
defined there under a misspelled namespace or a slightly different signature silently becomes a new function; and
a hidden friend gets moved out with the rest, which changes what lookup finds. This plan proposes `split_inline`, a
Clang-based rewriter that does the move: the class body becomes the synopsis, each definition follows the class
*inside the same namespace block*, and hidden friends stay where they are. Comments stay with their declarations
unless asked to travel: `--move-comments` carries existing `//!` markup to the definitions and places each under
the `\rSec` section its `\ref` group already names, so a header with specgen work in it keeps that work and
renders the same wording afterwards. The output compiles under the header's own flags and is a fixed point of the
tool.

## Why

- **Adoption.** The acid test (docs/acid-test.md) took a real header into specgen form by hand. Every library that
  wants generated wording has that step in front of it, and the step is the same each time.
- **No reopened namespace.** A member definition is safe anywhere: `optional<T>::reset()` must match a prior
  declaration or the compiler rejects it. A *free* function is not. Written unqualified inside
  `namespace beman::optional {` when the class lives in `beman::optional26`, or outside an `inline namespace v1`
  the class sits in, `bool operator==(const optional&, const optional&) { ... }` declares a second function and
  nothing complains until link time, or never. Placing the definitions after the class in the block that already
  encloses it leaves nothing to reopen, so the error can not be written.
- **Hidden friends are the interface.** A friend defined in the class is found by argument-dependent lookup only.
  Defined at namespace scope it is found by ordinary lookup too, which is a different interface. §1 and §6 already
  treat hidden friends as in-class by nature; the tool must not touch them.
- **The two halves already exist in the front end.** §3.4's synopsis splice turns an in-class definition into the
  declaration specgen shows; the out-of-line text is the same tokens with a qualifier, a template head, and no
  specifiers the standard forbids on a redeclaration. Writing the converter here reuses the splice, the comment
  classification, the compile-argument precedence, and the harness.

## Design

### What stays in the class

The declaration is the in-class text up to the body, spliced to `;`. The splice starts at the `:` of a written
mem-initializer list, else at the `{`, by the same rule as §3.4 (`spliced_tail_begin`,
`frontend/record_members.cpp`); what the tool leaves in the class is therefore exactly what `generate` would have
shown in the synopsis. Everything a redeclaration may not repeat, or that the standard reads from the first
declaration, stays here: attributes, `static`, `virtual`, `explicit`, default arguments, default template
arguments. So do `constexpr`/`consteval`, `noexcept`, and a trailing requires-clause, which are repeated on the
definition. Draft-form `//` comments stay. Doxygen stays: it documents the declaration, and a synopsis drops it
anyway (§1).

A function that is `= default` or `= delete` is not moved. A deleted function must be deleted on its first
declaration, and a defaulted one defined out of line is *user-provided*, which changes triviality and the implicit
`constexpr`; §6 lists both as declaration-only entities for the same reason.

### What is emitted

For each moved member, in class-body order:

1. The template heads, outermost first: one per enclosing class template, with default template arguments
   stripped and the head's requires-clause repeated verbatim, then the member's own head if it is a template. A
   member of a partial specialization takes the specialization's head and its written argument list; a member of
   an explicit specialization takes no `template<>` head at all.
2. `inline`, only when there is no template head and the function is not already `constexpr`, `consteval`, or
   `inline`. An in-class definition is implicitly inline; the out-of-line one in a header must say so.
3. The decl-specifiers as written, minus `static`, `virtual`, `explicit` and `explicit(...)`, and minus
   `[[...]]` attributes.
4. The declarator-id qualified by the chain of enclosing records: `optional<T, Alloc>::iterator::operator++`.
   The qualifier goes at the name's begin location, and that is one rule for every member kind: a destructor's
   name location is the `~`, a conversion function's is `operator`.
5. The parameter clause without default arguments, then the cv- and ref-qualifiers, `noexcept`, and the trailing
   requires-clause, copied.
6. The mem-initializer list and the body, byte for byte from the `:` or `{` to the closing `}`.

A return type that names a member of the class (`iterator begin()`) is not visible before the qualified name, so
the definition takes the trailing-return form: `auto optional<T, Alloc>::begin() -> iterator`. The tokens of the
type are preserved and the form is correct whether or not the type is dependent. Writing
`typename optional<T, Alloc>::iterator` instead is an open question below.

### Where it goes

After the `};` of the outermost class, inside the namespace block that encloses it, in class-body order; a nested
class's members interleave with the enclosing class's in source order. Nothing is reopened. A friend function
defined in the class, a static data member, an alias, a nested class definition, and a `static_assert` are not
members the tool moves, and are untouched.

### Comments, and `--move-comments`

Without the flag every comment stays where it is. The tool is a C++ restructuring and has no opinion about what a
comment means; specgen's three-way classification (§1) is specgen's. That is the form to run on a header *before*
specgen markup is written into it, which is the expected case: convert first, then document the definitions.

`--move-comments` is the form for a header that already carries specgen work, in-class docblocks and `\ref` groups
and perhaps a definition region with `\rSec` sections, and its contract is that nothing of that work is lost and
nothing has to be redone: `specgen generate` on the converted header renders what it rendered on the original
(oracle 2 below). Four rules do it.

**Which comment is a declaration's.** Clang's own attachment (`getRawCommentForDeclNoCache`, looked up through the
described template as §3.1 notes), which is also the attachment specgen reads. The spike checked its reach: it
attaches across a blank line, it attaches across an access specifier (`public:` has none of the `;{}#@` characters
Clang stops at), and a preprocessor directive between the two breaks it. The tool keeps the blank-line reach, since
specgen would have read that docblock as the member's and moving it is preserving it, and narrows the rest: if
anything but whitespace lies between the comment's end and the declaration's first token, the comment is not the
declaration's and stays. A docblock written above `public:` therefore stays on the declaration, where specgen
reports it, rather than moving to a definition it was never meant for.

**Which lines move.** The attached comment is one `RawComment`, and Clang coalesces adjacent `//` lines whatever
their third character is, so the block is split by line:

| Lines | Disposition |
| --- | --- |
| `//!` and `/*! ... */`, from the first such line to the end of the block | move with the definition |
| `///` and `/** ... */` above the first markup line | stay; Doxygen documents the declaration |
| a plain `//` line above the first markup line, the `// \ref{...}` group header among them | stays |
| a plain `//` line below the first markup line | moves; it is inside the chunk specgen reads either way |
| a trailing comment, code before it on its own line | stays, and never opens a moved run |

A block made only of markers (`\omit`, `\merge`, `\elsewhere`, `\expos`, `\seebelow`, `\freestanding`) moves like
any other. The first design kept it on the declaration, since §3.3 says such a block there is not an error, and
the `.same_wording` case for `spec_inclass_template.hpp` showed that to be wrong: `//! \merge` left on an in-class
declaration whose definition is out of line does not merge the member, while the same marker on the out-of-line
definition does (checked against the driver with a two-line header each way). The pre-pass sets of §3.3 read the
definition, so that is where the markers go. Doxygen stays because a synopsis drops it anyway (§1).

The last row is a trap the spike fell into: `void h() {} //! three.` followed on the next line by `/*! four */`
over `i()` coalesces into one comment attached to `i`, and the naive split moved `three` onto `i`'s definition. A
line with code before the comment belongs to that code's declaration, whatever follows it.

**Where the definition goes.** The member's group is the nearest `// \ref{stable}, title` comment above it in its
own class body (a nested class owns its own groups, §3.4). When the same namespace block already has a
`// \rSec<n>[stable]{...}` section after the class for that stable name, the definition goes into that section,
ordered among the definitions already there by the file offset of each one's in-class declaration (the chain is
`getCanonicalDecl()`); that is the order §3.3 already infers for in-class items relative to out-of-line siblings,
so the itemdescrs come out where they were. When no section exists for the group, the tool writes
`// \rSec<n>[stable]{Title}` after the last section already present, in class-body group order, with `n` taken
from the sibling sections (3 when there are none, `--section-depth` to override) and the title from the `\ref`
comment with its first letter capitalized; the author reviews the titles, which is a smaller edit than writing
the headers. A member with no group goes after the class and before the first section, as the plain form places
everything. Appending a moved definition right after `};` instead, ahead of the existing sections, would be wrong
for exactly the headers this flag exists for: an out-of-line definition is placed by its position in the `\rSec`
stream, and ahead of every section it has no section.

**Housekeeping.** A moved comment is removed as whole lines, so no indentation is left behind on the declaration's
line or on the `public:` below it (the spike left both), and is re-indented to column zero at its new home. One
blank line separates the items in a section, as the corpus writes them.

Doxygen never moves under this flag; a `--move-comments=all` that carries it too is an open question below.

### Refusals

The tool says no rather than guess, per member, and reports each on standard error with `file:line` in `generate`'s
diagnostic shape. The rest of the header is still converted and written; a refused member is left in the class,
which is still valid C++. Exit status 1 when anything was refused.

- A definition any part of which is in a macro expansion.
- A definition inside a conditional-compilation region that does not enclose the whole class. The preprocessor's
  skipped ranges are already recorded during the parse (§3.1, `SkippedRanges` in `frontend/file_views.hpp`); the
  taken branches' extents are the one addition. Moving a member out of its `#if` would drop the guard.
- A member of an unnamed class: there is no qualified name to write. A local class is never reached, since the tool
  reads namespace-scope classes only, so its members stay without a diagnostic.
- A member of an anonymous struct or union.

Deduced return types (`auto f() { ... }`) are not refused: an in-class body is already parsed after the class is
complete, in order, so a use before the definition fails the same way in both forms.

### Checking a conversion

Three oracles, all cheap given what the tree has:

1. **It compiles.** The tool re-parses its own output under the same compile arguments and refuses to write if the
   second parse reports errors the first did not.
2. **The wording does not change.** For a header in specgen form with in-class definitions (the shape
   `spec_inclass.hpp`, `spec_inclass_bodies.hpp`, and `spec_inclass_template.hpp` pin), converted with
   `--move-comments`, `specgen generate` on the converted header renders byte for byte what it renders on the
   original. Itemdecl text comes from the in-class
   declaration either way (§3.3), in-class items are already placed by class-body order relative to out-of-line
   siblings, and the bodies are the same tokens. For a header with no markup the synopsis `CodeText` is the
   invariant.
3. **It is a fixed point.** Running the tool on its output changes nothing.

The first is the tool's own guard; the second and third are ctest cases over specgen's corpus.

### Where it lives

A separate executable, `split_inline <header> [-o <file> | --in-place] [--move-comments] [-- <clang args>]`
(decided, 2026-11-08). The reason is the comments: specgen reads `//!` as markup, drops `///`, and keeps `//`, and a
converter running inside the same process would have to either share that classification or contradict it, and
either is confusing to the author reading a diagnostic. A tool of its own has one job and no opinion. What it
shares with specgen is the Clang front end's plumbing, not its model: the compile-argument precedence of
`frontend/compile_args` (the `--` tail, an explicit database, the walk up from the header), the parse, the LLVM pin,
and `spliced_tail_begin`. Whether it is built from this tree under `tools/split_inline/`, linking
`beman::specgen.frontend` for those, or from a repository of its own that ports the two helpers and carries its own
pin, is the second open question.

## The spike

475 lines against Clang 18: a walk over the namespace-level declarations, a raw-lexed token table of the main file
for the specifier and default-argument edits, and a `Rewriter` for the output. No `#if` detection, no formatting
of the result, no in-place write. Run on a 95-line header built to hold the cases above:

| | |
| --- | --- |
| In-class definitions moved | 20 |
| Hidden friends left in place | 2 (one a template) |
| `= default` / `= delete` left in place | 2 |
| Refusals | 0 |
| Output compiles, members instantiated and called | Clang 18 and GCC 13, `-Wall`, clean |
| Fixed point | yes |

The twenty cover constructors with mem-initializers, a destructor, a conversion function, operators, a static and a
virtual member, member templates with their own requires-clauses, members of a partial and of an explicit
specialization, and members of a nested class.

Source-location facts the plan rests on, each checked there:

- A destructor's `getNameInfo().getBeginLoc()` is the `~`; a conversion function's is `operator`. Inserting the
  qualifier at the name's begin is one rule.
- `ParmVarDecl::getDefaultArgRange()` covers the initializer only; the `=` is found by scanning back over
  whitespace, as `spliced_tail_begin` finds a constructor's `:`.
- A template parameter's source range includes its default argument. The first `=` at bracket depth zero cuts it,
  for type, non-type, and template template parameters alike.
- `TemplateParameterList::getRequiresClause()` is the class head's constraint, and it has to be repeated on every
  member's definition or the redeclaration does not match.
- `FunctionDecl::getReturnTypeSourceRange()` includes a reference return type's `&`, which the trailing-return
  rewrite relies on.
- Iterating `CXXRecordDecl::decls()` yields a `FriendDecl` for a friend defined in the class, so hidden friends are
  excluded by node kind; `methods()` never lists them.
- Clang 18 exposes an explicit specialization's written arguments through `getTypeAsWritten()`; Clang 19 and later,
  the pinned 23 among them, through `getTemplateArgsAsWritten()` on `ClassTemplateSpecializationDecl` as well.

Before, a constructor and an observer of a constrained class template with a defaulted parameter:

```cpp
template <class T, class Alloc = int>
    requires std::is_copy_constructible_v<T>
class optional {
  public:
    struct iterator { /* ... */ };
    // \ref{optional.ctor}, constructors
    //! \effects Constructs a disengaged optional.
    constexpr optional() noexcept : engaged_(false), value_() {}
    // \ref{optional.observe}, observers
    iterator begin() { return iterator{&value_}; }
    friend bool operator==(const optional& a, const optional& b) { return a.engaged_ == b.engaged_; }
    // ...
};
```

After:

```cpp
template <class T, class Alloc = int>
    requires std::is_copy_constructible_v<T>
class optional {
  public:
    struct iterator { /* ... */ };
    // \ref{optional.ctor}, constructors
    constexpr optional() noexcept;
    // \ref{optional.observe}, observers
    iterator begin();
    friend bool operator==(const optional& a, const optional& b) { return a.engaged_ == b.engaged_; }
    // ...
};

// \rSec3[optional.ctor]{Constructors}

//! \effects Constructs a disengaged optional.
template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
constexpr optional<T, Alloc>::optional() noexcept : engaged_(false), value_() {}

// \rSec3[optional.observe]{Observers}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
auto optional<T, Alloc>::begin() -> iterator { return iterator{&value_}; }
```

The spike's output keeps the body's original indentation and spacing; formatting is an open question below.

## Stages

Each stage lands green on its own: full ctest, `make lint`, goldens regenerated in the same commit, and, in this
tree, the version bumped (a minor bump when the executable first ships).

1. **The rewriter.** One component, `include/beman/specgen/split_inline/rewrite.hpp` and
   `src/beman/specgen/split_inline/rewrite.cpp`, its own library on the front end (per
   [component-recipe](../decisions/component-recipe.md), outside the module umbrella as the front end is): given
   a parsed header, the list of moved members, the refusals, and the edits as `{offset range, replacement}` pairs
   over the main file, with the
   out-of-line text for each member; with `--move-comments`, also the comment lines that travel, the section each
   definition lands in, and the headers to write. Pure over the AST; Catch2 tests parse snippets the way specgen's
   front-end component tests do. The splice is `spliced_tail_begin`; the comment split follows the marker-line
   split in `frontend/document_files.cpp`.
2. **The executable.** `split_inline` with the options above, `-o -` as the default, refusals on standard error,
   the re-parse guard, and the exit codes. Its compile arguments resolve exactly as `specgen generate`'s do, so a
   header that specgen can parse, `split_inline` can parse with the same command tail.
3. **Goldens.** `tests/split_inline/cases/<case>/input.hpp` and `expected.hpp`, compared with
   `cmake -E compare_files` as the driver's are (`tests/split_inline/run-case.cmake`), with a `.compiles` sibling
   that parses `expected.hpp` through the pinned Clang, a `.fixed_point` sibling, and, for the specgen-form cases
   converted with `--move-comments`, a `.same_wording` sibling comparing `specgen generate` over the two. Inputs for
   the cases the spike covered, each refusal, and the comment table row by row: the blank line, the access
   specifier, the trailing comment, a block of markers only, Doxygen above markup, a group with an existing section
   holding out-of-line siblings on both sides of the moved member, and a group with none. The Clang-free half,
   `apply_edits`, has a Catch2 test of its own.
4. **Documentation.** `docs/user-guide.md` gains a short *Converting an existing header* section pointing at the
   tool, placed before the authoring loop since it is the step that produces the shape *Start with one header*
   shows; and one decision record, `split-inline-in-place-namespace`: definitions stay in the class's own
   namespace block, hidden friends stay in the class, and the converter is not part of specgen.

## Open questions

### section-headers

Whether `--move-comments` writes a `// \rSec3[stable]{Title}` header for a `\ref` group that has no section yet.
**Recommendation: yes**, as designed above. The definitions have to be placed somewhere, and a header derived from
the group comment is a smaller hand edit (review a title) than the alternative (write the header, then move the
definitions under it). Without the flag nothing is written, since there is nothing to place by.

### move-comments-all

Whether a `--move-comments=all` carries Doxygen and plain comments along with the markup. **Recommendation: no.**
Doxygen documents the declaration, specgen drops it from the synopsis regardless, and an author converting
`@brief` prose into `//!` wording can read it where it is. Add it if a converted header shows the opposite need.

### tree

Whether `split_inline` builds from this tree, under `tools/split_inline/` linking `beman::specgen.frontend`, or
from a repository of its own. **Recommendation: this tree**, and the branch does that; moving it out later is a
matter of porting the two helpers. It gets the LLVM pin, the compile-argument
precedence, `spliced_tail_begin`, the corpus, the golden harness, and the release binary's build for free, and the
`.same_wording` oracle needs both executables in one ctest anyway. Its own repository is the right answer if the
tool is meant to serve projects that never see specgen; then the two shared helpers are ported, provenance noted,
and the pin is restated. Delivery is not what decides it: the GitHub Release archive and the `beman-specgen` wheel
are two packagings of one `cmake --install --component specgen_Runtime` tree, so a second executable in that
component lands in `bin/` beside `specgen`, and the wheel needs one more `install(PROGRAMS ...)` line, one more
`[project.scripts]` entry, and a shim that execs `data/bin/split_inline` instead of `data/bin/specgen`. The
release scripts name `bin/specgen` in a handful of places (the debug split, the portability check, the smoke
test) and would take the binary name as a parameter. The cost that is real is size: with
`BEMAN_SPECGEN_LLVM_LINK=static` each executable carries its own copy of Clang, so the archive and the wheel
roughly double.

### placement, name

Settled 2026-11-08: a separate executable, not a `specgen` subcommand, named `split_inline`. The subcommand was
recommended for the sharing listed under *Where it lives*; the user's ruling is that running the converter inside
specgen's process would at best be confusing about which tool's comment handling applied, and the sharing is had
by linking the front-end library instead.

### nested-return-type

Trailing return (`auto C<T>::f() -> iterator`) or a qualified type (`typename C<T>::iterator C<T>::f()`).
**Recommendation: trailing return.** It is one rule, preserves the type's tokens, and needs no `typename`
decision. The qualified form is what a hand conversion writes, but the qualifier has to be inserted *inside* the
type's tokens (`const iterator&` becomes `const typename C<T>::iterator&`), and `typename` is required only when the
type is dependent. Neither form reaches the wording: itemdecl text comes from the in-class declaration (§3.3).

### output

`-o <file>` with `-` as the default, plus `--in-place`. **Recommendation: both.** A conversion wants in-place; the
golden harness and a reviewer want a stream.

### formatting

Whether the tool formats what it writes. **Recommendation: copy bytes, then run the header's own `.clang-format`
over the inserted region**, through `clang::format::getStyle("file", ...)` and the formatting library the front end
already links (§3.6), with `--no-format` to skip it. The draft style `format_draft` applies to wording, not to a
user's source, so it is the wrong style here; laying the text out by hand would be a second formatter.

### conditional-compilation

A member whose declaration sits in an `#if` the class does not. **Recommendation: refuse** and say which directive.
Duplicating the guard around the definition is possible and is the thing to add if the acid-test header needs it.

### static-data-members

Whether `static constexpr` and `inline static` data members with in-class initializers move. **Recommendation:
no.** A `constexpr` static data member is implicitly inline and its out-of-line definition is deprecated; the draft
shows them in the synopsis as written.
