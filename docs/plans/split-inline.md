# Plan: `split_inline`, moving in-class definitions out of line

**Status:** PROPOSED, 2026-11-08. No issue yet; this document is the proposal. Two questions were settled with the
user the same day and are recorded in place: the tool is `split_inline`, a separate executable rather than a
`specgen` subcommand, so that specgen's own comment handling and the converter's never run in one process. A spike
against the distribution's Clang 18 (not the pinned 23) checked every source-location claim below, and the numbers
and the excerpt are from that run. The remaining questions at the end are the taste calls the user owns; each
records the recommendation it is asked with.

specgen's input contract (design §1) is a declaration-only class body with the definitions following it, out of
line. A header written the ordinary way defines its members where it declares them. Restructuring one by hand is
mechanical and has two classic failure modes: the definitions go into a *reopened* namespace, and a free function
defined there under a misspelled namespace or a slightly different signature silently becomes a new function; and
a hidden friend gets moved out with the rest, which changes what lookup finds. This plan proposes `split_inline`, a
Clang-based rewriter that does the move: the class body becomes the synopsis, each definition follows the class
*inside the same namespace block*, and hidden friends stay where they are. Comments stay with their declarations
unless asked to travel; the tool knows nothing of specgen's markup. The output compiles under the header's own
flags and is a fixed point of the tool.

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

### Comments

Every comment stays where it is. The tool is a C++ restructuring and has no opinion about what a comment means;
specgen's three-way classification (§1) is specgen's, and the one thing this tool must not do is guess at it.

`--move-comments` carries the comment block directly above a moved definition along with it, whatever its flavour,
and the class keeps a bare declaration. That is what a header already carrying `//!` docblocks on its in-class
definitions needs, since §3.3 attaches markup to the out-of-line definition, and it is what a Doxygen-documented
header does *not* want, since Doxygen documents the declaration. One flag, the author's call per run.

One fact the spike found bears on either setting. Clang coalesces adjacent `//` lines whatever their third
character is, so a `// \ref{...}` group comment and a `//!` docblock beneath it arrive as *one* `RawComment`
whose text starts with `// \ref`. The block that moves is the run of lines from the first `//!` or `/*!` line to
the end of the comment, or the whole comment when no such line is in it; the lines above that stay. The split is
the same shape as §3.2's split around `\rSec` lines (`frontend/document_files.cpp`). Without it the spike moved
nothing and wrote the docblock into the definition's group header.

The `\ref`-to-`\rSec3` header rewrite, which turns each class-body `// \ref{stable}, title` into the
`// \rSec3[stable]{Title}` the definition region needs, is **not** this tool's. The definitions come out in
class-body order, so the groups are contiguous and a hand or a specgen-side step can put the headers in; whether
specgen should grow that step is the first open question below.

### Refusals

The tool says no rather than guess, per member, and reports each on standard error with `file:line` in `generate`'s
diagnostic shape. The rest of the header is still converted and written; a refused member is left in the class,
which is still valid C++. Exit status 1 when anything was refused.

- A definition any part of which is in a macro expansion.
- A definition inside a conditional-compilation region that does not enclose the whole class. The preprocessor's
  skipped ranges are already recorded during the parse (§3.1, `SkippedRanges` in `frontend/file_views.hpp`); the
  taken branches' extents are the one addition. Moving a member out of its `#if` would drop the guard.
- A member of a local class or of an unnamed class: there is no qualified name to write.
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

1. **The rewriter.** One component, `split_inline/rewrite.hpp`/`.cpp` (per
   [component-recipe](../decisions/component-recipe.md) if it lives here): given a parsed header, the list of moved
   members, the refusals, and the edits as `{offset range, replacement}` pairs over the main file, with the
   out-of-line text for each member. Pure over the AST; Catch2 tests parse snippets the way specgen's front-end
   component tests do. The splice is `spliced_tail_begin`; the comment split follows the marker-line split in
   `frontend/document_files.cpp`.
2. **The executable.** `split_inline` with the options above, `-o -` as the default, refusals on standard error,
   the re-parse guard, and the exit codes. Its compile arguments resolve exactly as `specgen generate`'s do, so a
   header that specgen can parse, `split_inline` can parse with the same command tail.
3. **Goldens.** `tests/golden/split_inline/<case>/input.hpp` and `expected.hpp`, compared with
   `cmake -E compare_files` as the others are, with a `.compiles` sibling that parses `expected.hpp` through the
   pinned Clang, a `.fixed_point` sibling, and, for the specgen-form cases converted with `--move-comments`, a
   `.same_wording` sibling comparing `specgen generate` over the two. Inputs for the cases the spike covered plus
   each refusal.
4. **Documentation.** `docs/user-guide.md` gains a short *Converting an existing header* section pointing at the
   tool, placed before the authoring loop since it is the step that produces the shape *Start with one header*
   shows; and one decision record, `split-inline-in-place-namespace`: definitions stay in the class's own
   namespace block, hidden friends stay in the class, and the converter is not part of specgen.

## Open questions

### section-headers

Whether anything writes the `// \rSec3[stable]{Title}` headers from the class body's `// \ref{stable}, title`
groups, now that `split_inline` does not. **Recommendation: nothing, for now.** The author writes the titles
anyway, the definitions come out grouped, and a specgen `--scaffold-sections` step is a separate small proposal if
hand-writing them proves to be the slow part of the acid test.

### tree

Whether `split_inline` builds from this tree, under `tools/split_inline/` linking `beman::specgen.frontend`, or
from a repository of its own. **Recommendation: this tree.** It gets the LLVM pin, the compile-argument
precedence, `spliced_tail_begin`, the corpus, the golden harness, and the release binary's build for free, and the
`.same_wording` oracle needs both executables in one ctest anyway. Its own repository is the right answer if the
tool is meant to serve projects that never see specgen; then the two shared helpers are ported, provenance noted,
and the pin is restated.

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
