<!-- markdownlint-disable MD013 -->

# specgen user guide

`beman.specgen` generates C++ standard-library specification wording from a
structured header. `generate` reads declarations with Clang and lowers them to
the tool's JSON intermediate representation (IR); `render` translates that IR
to draft LaTeX, mpark/wg21 markdown, or wg21org org. The executable always
builds with the Clang front end, but rendering saved IR invokes no compiler at
run time.

This guide starts with one small header, then covers the authoring and paper
integration workflows that have proved useful in `beman.transcode` and
`beman.transpose`. The remaining sections are the command and markup reference.

## Requirements and build

The project requires GCC 16 with C++26 libstdc++, CMake 3.30 or later, `uv`,
and the LLVM/Clang 23.1 development packages. The LLVM version is exact, not a
minimum: the Clang C++ API is not stable between releases.

The normal build includes the front end, IR, and all renderers:

```sh
uv run cmake --preset gcc-release
uv run cmake --build --preset gcc-release
uv run ctest --preset gcc-release
```

`find_package` asks for the version named by `BEMAN_SPECGEN_LLVM_VERSION`
(`23.1` by default), even when a newer LLVM is installed. Pass
`-DClang_DIR=<prefix>/lib/cmake/clang` for an installation outside the normal
search path. Moving the pin with
`-DBEMAN_SPECGEN_LLVM_VERSION=<major>.<minor>` is a deliberate source-porting
operation, not a compatibility promise. There is no build of specgen without
the front end. The repository also supports its day-to-day Makefile build with
`make TOOLCHAIN=gcc-16 test`.

The preset build writes the executable to
`build/gcc-release/tools/specgen/specgen`; the Makefile build writes a
configuration-specific executable below `.build/build-gcc-16/tools/specgen/`.
Add the selected directory to `PATH` or invoke the executable there.

`make install` installs the executable as `.install/bin/specgen` (with the
library, headers and CMake package beside it), and that path is the one the
examples assume. `PREFIX` selects a different prefix. Installing from
the preset build tree needs an explicit `--prefix`, because that tree is
configured with `/usr/local`.

## Start with one header

The smallest useful input has a declaration-only class, draft-form `\ref`
group comments in the class body, matching `\rSec` sections, and docblocks on
the out-of-line definitions:

```cpp
namespace demo {

class widget {
  public:
    // \ref{widget.cons}, constructors
    widget();
    explicit widget(int value);

    // \ref{widget.observers}, observers
    bool empty() const;

  private:
    int value_ = 0;
};

// \rSec3[widget.cons]{Constructors}

//! \effects Constructs a `widget` holding no value.
widget::widget() : value_(0) {}

//! \effects Constructs a `widget` holding `value`.
widget::widget(int value) : value_(value) {}

// \rSec3[widget.observers]{Observers}

//! \returns `true` if the widget holds no value, `false` otherwise.
bool widget::empty() const { return value_ == 0; }

} // namespace demo
```

The class body supplies the synopsis. Its `\ref` comments say which clause
describes each member. The definitions supply both clause order and the
wording attached to each declaration. specgen takes code spelling from the
source tokens; it does not pretty-print the AST back into a guessed interface.

Render the complete document while editing:

```sh
specgen generate widget.hpp --backend mpark --validate \
  --no-compile-commands -- -std=c++2c
```

The complete checked example is
[`tests/corpus/spec_widget.hpp`](../tests/corpus/spec_widget.hpp). For a real
library, replace the explicit arguments with the library's compilation
database or a deterministic argument list containing its include and generated
header directories.

## The authoring loop

Start with document structure, then fill in entities. This avoids discovering
late that clause boundaries and fragment boundaries disagree:

1. Choose the document root. A single implementation header can be one
   document. An umbrella header can gather the public headers included inside
   its `.syn` region; includes outside that region remain implementation.
2. Write the `\rSec` outline in paper order. The shallowest sections become
   separate files under `--split`; deeper sections remain nested in their
   parent's fragment.
3. Put `\ref` group headers in class synopses, or use `\at` where source order
   does not express the intended clause.
4. Add docblocks to definitions and mark every declaration that is deliberately
   not an ordinary specification entity with `\expos`, `\omit`, or `\merge`.
5. Run `generate --validate` frequently. Read standard error as well as the
   exit status: docblock errors are findings on usable input and do not by
   themselves stop IR emission.
6. Inspect all three backends before settling taste-sensitive phrasing. They
   share semantics, but tables, code spans, headings, and paragraph framing are
   necessarily visible in different syntax.

Derivation is most useful when the implementation already says exactly what
the specification should say. Prefer `\returns-equiv`, `\effects-equiv`, and
derived constraints or mandates in that case. Use authored prose when exposing
the body would leak implementation strategy, when the specification is more
abstract than the implementation, or when the derived phrasing is simply the
wrong contract.

## Generate and render

The shortest end-to-end invocation is one command. `generate` parses the
header, builds the document, validates it, and renders it in a single pass; the
IR stays in memory and is never written anywhere:

```sh
specgen generate header.hpp --validate --backend latex -o wording.tex -- -Iinclude
```

The same work splits into two commands when the IR itself is wanted — to store
it, to inspect it, or to render it later on a machine with no Clang:

```sh
specgen generate --emit-ir header.hpp -o wording.json -- -Iinclude
specgen render --from-ir wording.json --validate --backend latex -o wording.tex
```

Standard input can connect those two without a temporary file:

```sh
specgen generate --emit-ir header.hpp -- -Iinclude |
  specgen render --from-ir - --validate --backend latex
```

All three produce the same wording, byte for byte; the golden suite's
`.singlepass` cases pin that for every corpus header.

Its supported generation path is:

```text
specgen generate <header> [--emit-ir]
                 [--backend latex|mpark|org]
                 [--validate] [--paper] [--new-root <name>]
                 [--base-heading-level <n>] [--base-section-depth <n>]
                 [--split <dir> [--root <name>]]
                 [-o <file>]
                 [--compile-commands <dir> | --no-compile-commands]
                 [-- <clang arguments>...]
```

Without `--emit-ir`, `generate` renders wording, and every option above that
steers a backend means exactly what it means for `render`. `--emit-ir` emits
the complete JSON document instead and stops before the backends, so it rejects
those options rather than ignoring them. `-o` and
`--output` name the destination either way; otherwise output goes to standard
output. With no header at all, `generate` parses a stock snippet as a
front-end link probe and produces no wording.

Compilation arguments have this precedence:

1. Everything after the first bare `--` is passed to Clang verbatim.
2. `--compile-commands <dir>` reads the entry for the header from that
   directory's `compile_commands.json`.
3. By default, specgen searches above the header for a compilation database.
4. If none of those supplies arguments, the front end uses its fixed defaults.

`--no-compile-commands` disables only the default search. It has no effect when
an explicit directory or a `--` argument tail already takes precedence. When a
database supplies flags, specgen reports the selected entry's working directory
on standard error.

Use `dump-decls` to inspect the main-file declaration/comment interleave used by
the front end:

```sh
specgen dump-decls header.hpp -- -Iinclude
specgen dump-decls header.hpp --compile-commands build
```

It accepts the same compilation-database options and `--` tail as `generate`.
This is a debugging command: when Clang reports a parse error, it warns and
prints the possibly incomplete interleave instead of rejecting it.

## JSON IR and rendering

The JSON file is the boundary between parsing and presentation. It can be
stored, inspected, read back, and rendered repeatedly without Clang. A basic
round trip is:

```sh
specgen generate --emit-ir header.hpp -o wording.json -- -Iinclude
specgen render --from-ir wording.json --backend latex -o wording.tex
specgen render --from-ir wording.json --backend mpark -o wording.md
specgen render --from-ir wording.json --backend org -o wording.org
```

`render` accepts these options:

```text
specgen render --from-ir <file|->
               [--backend latex|mpark|org]
               [--validate]
               [--paper] [--new-root <name>]
               [--base-heading-level <n>] [--base-section-depth <n>]
               [-o|--output <file>]
               [--split <dir> [--root <name>]]
```

The default backend is `latex`. The `mpark` backend emits pandoc markdown for
the mpark/wg21 framework; `--paper` also wraps the fragment in an
editing-instruction `::: add` div and is valid only with that backend. The `org`
backend emits org for the wg21org exporter.

`--split <dir>` writes one file per top-level section and prints an ordered
manifest of written paths to standard output. File stems are stable names and
extensions follow the backend: `.tex`, `.md`, or `.org`. Nodes outside every
section go into a root fragment. Its name is normally the longest common dotted
prefix of the section names; use `--root <name>` when it cannot be derived or
must be overridden. `--root` is valid only with `--split`, and `--split` cannot
be combined with `--output`.

Splitting does not delete files left by an earlier run. Consumers should use
the manifest, in document order, to reconcile the output directory.

### Where a top-level section starts

A fragment's sections have to sit under the heading in your paper that
introduces them, and where that is depends on how your paper is written. Two
options say so, one per unit, because the backends do not measure the same
quantity:

- `--base-heading-level <n>` is the markdown or org heading level of a
  top-level section, for the `mpark` and `org` backends. It is `2` by default,
  which is the level the mpark/wg21 examples write their own sections at. A
  paper that writes its sections at `##` and introduces the wording with a
  `##` heading wants `3` here, so the generated clauses become children of that
  heading rather than siblings of it.
- `--base-section-depth <n>` is the `\rSec` depth of a top-level section, for
  the `latex` backend. It is `3` by default, the draft's own library split
  granularity.

Each is rejected on the backends that do not measure in its unit, rather than
ignored. Both take a whole number of at least 1; under `mpark` the level may
not exceed 6, markdown's deepest heading. The base moves only where the
outermost section sits: nested sections still descend one step at a time from
it, and `--split` starts every fragment at the same base a whole render would.

### Proposed stable names in mpark papers

The mpark/wg21 framework's `.sref` class looks stable names up in the current
working draft. A paper's newly proposed clauses are not there yet, so leaving
`.sref` on them produces warnings and links to pages that do not exist. Name
each proposed stable-name subtree with `--new-root`:

```sh
specgen render --from-ir wording.json --backend mpark \
  --new-root transcode --new-root null.term
```

The option is mpark-only and repeatable. It removes `.sref` from an exact root
and every stable name below it, whether the name occurs on a heading or in a
cross-reference. References to existing standard clauses retain `.sref` and
continue to resolve normally. This is deliberately narrower than stripping
the class from every stable name in a paper.

### Integrating generated fragments into a paper

Both `beman.transcode` and `beman.transpose` treat rendered fragments as build
artifacts with reviewable source history. Their durable pattern is:

1. Keep one script as the only place that invokes specgen. Give it an explicit
   list of document roots, root-fragment names, compiler arguments, backend
   options, and proposed stable-name roots.
2. Generate through IR when more than one render or a separate validation pass
   is useful. Otherwise the single-pass `generate` command is equivalent.
3. Render with `--split` and preserve the printed manifest. Directory order is
   not document order, and stale files are not pruned by specgen.
4. Generate into a scratch or staging directory for checks, then diff against
   the committed fragments. Reconcile deleted files from the manifest rather
   than trusting what an earlier run left in the output directory.
5. Transclude fragments into the paper in manifest order. Edit header markup,
   never the rendered files.
6. Record the specgen revision used while the tool is under development. A
   wording diff should say whether the header changed, the generator changed,
   or both.

A minimal generation core looks like this:

```sh
specgen generate --emit-ir include/beman/example/example.hpp \
  --no-compile-commands -o example.json -- \
  -std=c++2c -Iinclude -I.build/generated/include

specgen render --from-ir example.json --backend mpark --validate \
  --paper --new-root example \
  --split papers/wording --root example.syn \
  > papers/wording/example.manifest
```

Use `--paper` when the whole fragment is an addition and should receive
editing-instruction framing and `x`, `x+1`, ... paragraph numbers. Omit it when
the surrounding paper supplies that framing or when ordinary wording blocks
are wanted.

There are two common document layouts:

- A library with one specification-facing header per proposed header can put
  each header's `.syn` fence and clauses in that file, then run specgen once per
  header into a shared staging directory. Give every run an explicit `--root`
  so its loose synopsis nodes cannot collide.
- An umbrella header can put its public component includes inside one gathered
  `.syn` region and its clause outline after the fence. specgen follows those
  includes as part of one document. This lets implementation stay split across
  normal component headers without merging source files for the generator.

Committing fragments lets a paper build without the pinned LLVM toolchain and
makes wording changes visible in review. A cheap CI staleness gate may hash the
document roots and the headers included inside each gathered `.syn` region;
that proves the committed output came from the current inputs. It does not
replace regeneration plus `--validate`, which answer whether the output and
the specification are correct.

## Comment forms and document structure

Comment spelling is significant:

- `//!` and `/*! ... */` are specgen docblocks. Their contents are parsed as
  elements and structural markers.
- `///` and `/** ... */` are Doxygen. They are removed from synopses and
  extracted bodies but never promoted into specification wording.
- Plain `//` and `/* ... */` comments are draft-form comments. They survive
  where the corresponding source is rendered.

A Doxygen-only header is valid input and can produce empty wording. Validation
then reports its public declarations as undocumented. Those coverage errors are
intentional: successful parsing must not silently reinterpret Doxygen prose.

Draft-form section markers build the document tree:

```cpp
// \rSec3[optional.ctor]{Constructors}
```

The depth controls nesting, the bracketed value is the stable name, and the
braced value is the title. A numbered draft-style heading ending in a stable
name, such as `// 22.5.3.3 Destructor[optional.dtor]`, is not enough because it
has no depth; specgen warns and suggests the `\rSec<depth>[stable]{title}` form.
A title too long for one line may continue on the immediately following plain
`//` lines — the shape `clang-format` produces when it wraps a long marker —
and the wrapped lines join back into one title with single spaces. A `{title}`
that never closes is still reported as a malformed marker.

Inside a class, a draft-form group header routes following documented in-class
members to the section with the matching stable name:

```cpp
// \ref{optional.ctor}, constructors
```

An explicit `\at stable.name` marker on a member overrides this inferred route.

A gathered header synopsis can span files. The declarations of every header
`#include`d **inside** the region are part of the document, in the order their
includes appear, so a header that is an umbrella of includes specifies the
whole of what it includes there:

```cpp
// \rSec2[transcode.syn]{Header `<transcode>` synopsis}
#include <beman/transcode/error.hpp>
#include <beman/transcode/concepts.hpp>
/// END [transcode.syn]

#include <beman/transcode/detail/labels.hpp>   // outside: implementation

// \rSec2[transcode.errors]{Error types}
```

An include outside the region is invisible, as everything reached by an
include was before. A followed header carries no `\rSec` markers of its own —
its clauses are the umbrella's, after the fence — and its declarations reach
them by routing.

A gathered header synopsis routes the same way. A documented declaration folded
into the region contributes its declaration to the synopsis, and its
description goes to the section `\at` names, or to the one named by the `\ref`
group header standing over it. With neither, the description is rendered beside
the synopsis, in the section the region itself is in — which is how a range
adaptor object gets a clause of its own without leaving the header synopsis it
belongs in. A route that names no section is a validation error, the
same as one written on a class member.

An in-class member definition is reduced to a declaration in the synopsis — a
body is never synopsis content — and the member is described in its own
subclause like any other. Declaring in class and defining out of line remains
the recommended style (the definition's lexical position is what places its
wording), but it is not required for a clean synopsis.

A class or class-template definition's own docblock describes the *type*. Its
description elements are rendered immediately after the class synopsis, with no
item declaration of their own — the shape the draft uses for a class's general
subclause. An authored `\mandates` there replaces the paragraph derived from the
class's direct `static_assert`s. A `\at stable.name` on the class definition
routes both that derived paragraph and the class's own description to the named
section, in that order. This is particularly useful for a class gathered from
a component header whose `\rSec` outline lives in an umbrella header. A route
to a section that does not exist is a validation error. `\verbatim-itemdecl`
and the `*-equiv` extraction markers are errors on a class definition: the
first has `\verbatim-synopsis` as its class-level counterpart, and the second
needs a function body to extract.

A docblock documents the declaration that follows it. Wording comes from class
and class-template definitions (a synopsis, the class's own description, and
routed members), documented
function *definitions*, documented in-class type aliases, and — at namespace
scope — documented aliases, alias templates, variables, variable templates,
concepts, enumerations, and record declarations the header never defines (an
undefined class-template primary renders as its own declaration). A documented
enumeration's item declaration is the enumeration as written, enumerator list
included, and its description is what the enumerators mean. A docblock on any
other entity kind, or on a function declaration rather than its definition,
is reported as an error: it would otherwise produce no wording, silently.

## Description elements

A specgen docblock contains only the elements that apply. The complete element
tag vocabulary, in canonical output order, is:

```text
\constraints  \mandates  \expects  \hardexpects  \effects  \sync
\ensures      \result    \returns  \throws       \complexity
\remarks      \errors
```

The display labels include *Preconditions* for `\expects`, *Hardened
preconditions* for `\hardexpects`, *Postconditions* for `\ensures`, and *Error
conditions* for `\errors`. Authored order does not control output order; an
out-of-order element produces a note and output is canonicalized. Duplicate
elements produce a warning and are retained. Unknown tags are errors.

Text after a tag begins its first paragraph. A blank decorated line separates
paragraphs, and the next tag ends the current element. Backticks mark code.
Use `\iref{stable.name}` outside backticks for a prose reference; it may point
to a standard subclause outside the generated document.

An element can end in one authored itemization:

```cpp
//! \constraints All of the following are true:
//! \item `T` is complete, and
//! \item `T` meets the requirements in \iref{some.requirements}.
```

An item's following nonblank lines continue that item. After the first
`\item`, use another `\item` or begin a new element; the IR cannot place more
prose after the list.

An element can instead end in one two-column table:

```cpp
//! \effects See the following table.
//! \lib2dtab2[optional.assign.copy]{Assignment effects}
//! \column source has a value
//! \column source has no value
//! \row destination has a value
//! \cell assigns the contained value.
//! \cell destroys the contained value.
//! \endlib2dtab2
```

The table requires exactly two `\column` entries, one or more `\row` entries,
and exactly two `\cell` entries per row. Non-tag lines continue the active
caption, column heading, row heading, or cell. `\endlib2dtab2` is required, and
the table is terminal within its element.

Use `\libtab2` for a flat two-column table such as an enumeration's meanings.
Here the text on `\row` is the first column and the row has one `\cell` for the
second:

```cpp
//! \remarks The constants have the meanings shown in the following table.
//! \libtab2[example.errc]{Meaning of `errc` constants}
//! \column constant
//! \column meaning
//! \row `ok`
//! \cell No error occurred.
//! \row `invalid`
//! \cell The input was invalid.
//! \endlibtab2
```

`\lib2dtab2` is a cross-product table: each `\row` is a row heading followed by
two data cells. `\libtab2` is the ordinary two-column form: each `\row` is its
first data cell followed by one `\cell`. Both require two column headings and
at least one row, and both are terminal within their element.

## Extraction markers

- `\effects-equiv` extracts a function body as an *Effects: Equivalent to:*
  block. It removes consumed leading `static_assert`s, inactive conditional
  branches, preprocessing directive lines, specgen comments, and Doxygen
  comments. Draft-form comments remain.
- `\returns-equiv` extracts the expression from a single-return body as the
  *Returns:* wording.
- `\constraints-in-decl` retains the requires-clause in the item declaration
  instead of deriving a separate *Constraints:* element.

`\effects-equiv` and authored `\effects` are mutually exclusive, as are
`\returns-equiv` and authored `\returns`.

By default, associated constraints are removed from the item declaration and
rendered as derived *Constraints*. A leading run of body-local type aliases
followed by `static_assert`s produces derived *Mandates*; the aliases remain in
an extracted equivalent-to body and the consumed assertions do not. Authored
`\constraints` or `\mandates` replaces its derived wording while retaining the
derivation as validation evidence for drift checks.

## Placement and grouping markers

- `\omit` excludes a declaration completely.
- `\merge` suppresses a declaration that is represented by another
  specification entity. It removes the marked declaration from synopsis and
  wording; on a namespace-scope record definition it suppresses the whole
  record contribution.
- `\describe` forces an item declaration for a documented `= default` or
  `= delete` entity.
- Bare `\also`, or an empty docblock, joins an overload's signature to the
  preceding described item in the same section.
- `\group id` names a group primary. `\also id` joins that earlier primary in
  the same section when the declarations are not adjacent. Targets are
  resolved left to right, so forward and cross-section targets are invalid.
- `\at stable.name` routes an in-class documented member, a folded-in
  namespace entity, or a class's own wording to that section, overriding the
  inferred placement. On a class it carries both the class-scope mandates
  paragraph and the class description.

An `\also` block should contain no description elements. `\group` and
`\also` are mutually exclusive in the same docblock.

## Synopsis and spelling markers

specgen normalizes the spellings the draft is strict about, so a header may use
its own house style. `T const&` and `const T&` both render `const T&`, and
`T *p` renders `T* p`; whichever the library writes, the wording says what
[structure.specifications] says.

- `\expos` marks an entity exposition-only. The default spelling removes a
  trailing underscore and changes underscores to hyphens; `\expos(name)`
  supplies the exact exposition name. Namespace-scope concepts, variable
  templates, variables, enumerations, aliases, alias templates, and class
  templates, as
  well as class members, can be exposed. An exposition-only enumeration's uses
  — its type, and a qualified enumerator — render under the exposition name
  too. A **nested class** can be exposed as well, and with bare `\seebelow`
  it renders as a declaration — `class $iterator$; // exposition only` — which
  is how the draft writes a view's iterator; its own members are exposition
  with it, so an extracted body that names one says the exposition name. A partial or explicit specialization
  follows its primary and needs no marker of its own: it is the same entity,
  and renders under the same exposition name. The marked declaration may live in an
  included implementation header outside the document extent: uses in the
  specified document still render as `\exposid`, so moving machinery into a
  `detail/` header does not force it back into the public header. The marker
  does not pull that declaration into the document; only its reached uses are
  rewritten. A public component header included inside a gathered `.syn`
  region is different: it is part of the document extent, so its declarations
  contribute to that synopsis.
- Bare `\seebelow` masks a function return type — a leading one whole, an
  explicit trailing one as `auto f(...) -> see below;`, keeping the trailing
  shape. `\seebelow noexcept` and
  `\seebelow explicit` mask only the condition inside that specifier. On a
  documented in-class type alias, bare `\seebelow` masks the complete RHS.
  On a documented namespace-scope variable or variable template, bare
  `\seebelow` masks the declared type as *unspecified* and drops the
  initializer — the customization-point-object shape,
  `inline constexpr unspecified name;`. Marked `\expos` as well, the two
  compose: `inline constexpr unspecified $name$; // exposition only`. The
  targeted forms do not apply to a variable either way, and saying one is an
  Error. On a documented namespace-scope concept, bare `\seebelow` masks the
  constraint-expression, as it does an alias's right-hand side; both compose
  with `\expos` the same way, rendering
  `using $name$ = see below; // exposition only`. An enumeration accepts no
  `\seebelow` in any form: the draft spells neither an enum-base nor an
  enumerator list *see below*, and a marker that silently did nothing left a
  description contradicting the synopsis beside it.
- `\impdef` masks a documented in-class type alias RHS as
  *implementation-defined*. It applies only to aliases and is mutually
  exclusive with `\seebelow`.
- `\freestanding` and `\freestanding-deleted` add the corresponding literal
  draft comment to a function declaration in the class synopsis.
- `\verbatim-synopsis` is terminal. All following decorated lines form one
  exact, span-free synopsis without C++ parsing or formatting. Written as a
  class definition's own docblock (no blank line before the class), the
  authored text *replaces* the extracted synopsis while the class's members,
  roster, and routing are processed as usual; detached, the block stands
  alone as an anonymous synopsis.
- `\verbatim-itemdecl` is terminal. Authored elements before it remain the
  item description; all following decorated lines form one exact, span-free
  item declaration. Multiple declaration lines are not split, parsed,
  formatted, indexed, or interpreted as draft markup. Written as a
  declaration's own docblock, the authored text *replaces* that
  declaration's extracted item declaration — the item appears once; a
  detached block stands alone, and pairs with `\omit`/`\merge` on the real
  declaration when one exists (as the hash-specialization pattern does).

## Validation and diagnostics

Run `render --validate` in the normal authoring loop. Validation checks:

- IR span and authored table structure.
- Coverage: each declaration must be described, exposition-only, defaulted or
  deleted, explicitly omitted, or represented by a merged declaration; routed
  wording must name a section that exists. A *public data member* needs none of
  those: its declaration is its specification, the way the draft writes
  `from_chars_result`, and a class's own description may name it.
- Leakage: wording, item declarations, equivalent-to bodies, tables, and
  synopses must not name invisible members, surviving implementation namespace
  qualifiers, or bare names resolved to declarations outside this document.
  A marked `\expos` declaration may live in an included implementation header;
  otherwise rewrite the contract in documented terms or use authored prose.
  A foreign helper and a declaration in the document may legitimately share a
  spelling, such as an enumerator and a generated lookup table; the document's
  declaration wins, so that collision alone is not reported as leakage.
- Local references in synopsis code must name generated sections. Prose
  `\iref` references may deliberately name external standard subclauses.
- Authored *Constraints* and *Mandates* must not duplicate or contradict their
  suppressed derived conjuncts.
- An authored *Throws:* paragraph must not contradict an unconditionally
  `noexcept` signature. There is deliberately no reverse check.
- In a class that already exposes members, unmarked private data is noted as a
  possible missing `\expos` marker.
- A hidden helper function used only by an unextracted body is noted unless a
  rendered fragment already leaks the same name, in which case leakage reports
  the error.

Docblock and source-structure diagnostics are printed by `generate` as
`<header>:<line>: <severity>: <message>`. Examples include element-order notes,
duplicate-element warnings, unknown-tag errors, malformed `\rSec` markers,
unrecognized draft-style headings, and errors for a docblock on an entity kind
that produces no wording (a namespace alias, or a function declaration whose
markup belongs at the definition). These diagnostics describe markup on a
successfully parsed header, so even an error does not prevent IR emission or
change `generate`'s successful exit status. Always inspect standard error.

`--validate` prints findings as `specgen: <context>: <severity>:
<message>`. Notes and warnings are printed and rendering continues. If any
finding has error severity, every finding is printed, rendering is skipped,
and the command exits 1.

## Exit behavior

- Successful `generate`, `render`, and `dump-decls` invocations exit 0.
- Command-line usage errors and unavailable Clang-only commands exit 2.
- `generate` exits 1 when the header cannot be read, Clang cannot build an AST,
  Clang reports a parse error, or output cannot be written. It never emits
  plausible partial wording after a C++ parse failure. Rendering without
  `--emit-ir` adds `render`'s own failures: fragment errors and, under
  `--validate`, error-severity findings.
- `render` exits 1 on unreadable or invalid JSON, output failures, fragment
  errors, or error-severity validation findings.
- `dump-decls` is diagnostic by design: after a recoverable Clang parse error
  it warns, prints the partial interleave, and exits 0.

Use `specgen --help` for the current command-line summary and
`specgen --version` for the installed version.
