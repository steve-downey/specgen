<!-- markdownlint-disable MD013 -->

# specgen user guide

`beman.specgen` generates C++ standard-library specification wording from a
structured header. The Clang-enabled `generate` command translates a header to
the tool's JSON intermediate representation (IR), and the portable `render`
command translates that IR to draft LaTeX, mpark/wg21 markdown, or wg21org org.

## Requirements and build

The project requires GCC 16 with C++26 libstdc++, CMake 3.30 or later, and
`uv`. Generating IR also requires the Clang 22 development package; the
supported front-end configurations use Clang 22 or 23 with GCC 16's libstdc++.

A portable build includes the IR and all renderers:

```sh
uv run cmake --preset gcc-release
uv run cmake --build --preset gcc-release
uv run ctest --preset gcc-release
```

specgen requires LLVM/Clang 22's development install; `find_package` asks for
that version by default (`BEMAN_SPECGEN_LLVM_VERSION`, `22.1`) and so locates
it even alongside a newer LLVM, `-DClang_DIR=<prefix>/lib/cmake/clang` points
at one off the default search path, and `-DBEMAN_SPECGEN_LLVM_VERSION=<major>.<minor>`
moves the pin. There is one configuration — there is no build of specgen without
the front end. The repository also
supports its day-to-day Makefile build with `make TOOLCHAIN=gcc-16 test`.

The preset build writes the executable to
`build/gcc-release/tools/specgen/specgen`; the Makefile build writes a
configuration-specific executable below `.build/build-gcc-16/tools/specgen/`.
Add the selected directory to `PATH` or invoke the executable there.

`make install` installs the executable as `.install/bin/specgen` (with the
library, headers and CMake package beside it), and that path is the one the
examples assume. `PREFIX` selects a different prefix. Installing from
the preset build tree needs an explicit `--prefix`, because that tree is
configured with `/usr/local`.

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
                 [--validate] [--paper]
                 [--split <dir> [--root <name>]]
                 [-o <file>]
                 [--compile-commands <dir> | --no-compile-commands]
                 [-- <clang arguments>...]
```

Without `--emit-ir`, `generate` renders wording, and `--backend`, `--validate`,
`--paper`, `--split` and `--root` mean exactly what they mean for `render`.
`--emit-ir` emits the complete JSON document instead and stops before the
backends, so it rejects those five options rather than ignoring them. `-o` and
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
               [--paper]
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

An in-class member definition is reduced to a declaration in the synopsis — a
body is never synopsis content — and the member is described in its own
subclause like any other. Declaring in class and defining out of line remains
the recommended style (the definition's lexical position is what places its
wording), but it is not required for a clean synopsis.

A docblock documents the declaration that follows it. Wording comes from class
and class-template definitions (a synopsis plus routed members), documented
function *definitions*, documented in-class type aliases, and — at namespace
scope — documented aliases, alias templates, variables, variable templates,
concepts, and record declarations the header never defines (an undefined
class-template primary renders as its own declaration). A docblock on any
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
- `\at stable.name` routes an in-class documented member to that section,
  overriding the nearest `\ref{stable.name}` group.

An `\also` block should contain no description elements. `\group` and
`\also` are mutually exclusive in the same docblock.

## Synopsis and spelling markers

- `\expos` marks an entity exposition-only. The default spelling removes a
  trailing underscore and changes underscores to hyphens; `\expos(name)`
  supplies the exact exposition name. Namespace-scope concepts, variable
  templates, variables, aliases, and alias templates, as well as class
  members, can be exposed.
- Bare `\seebelow` masks a function return type — a leading one whole, an
  explicit trailing one as `auto f(...) -> see below;`, keeping the trailing
  shape. `\seebelow noexcept` and
  `\seebelow explicit` mask only the condition inside that specifier. On a
  documented in-class type alias, bare `\seebelow` masks the complete RHS.
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
  wording must name a section that exists.
- Leakage: wording, item declarations, equivalent-to bodies, tables, and
  synopses must not name invisible members or surviving implementation
  namespace qualifiers.
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
that produces no wording (an enum, or a function declaration whose markup
belongs at the definition). These diagnostics describe markup on a
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
