<!-- markdownlint-disable MD013 -->

# specgen user guide

`beman.specgen` generates C++ standard-library specification wording from a
structured header. Its `generate` command parses the header with Clang, reads
the specgen markup attached to the declarations, and renders the wording as
draft LaTeX, mpark/wg21 markdown, or wg21org org.

This guide begins with one small header and then describes the authoring and
paper-integration workflows that have proved useful in `beman.transcode` and
`beman.transpose`. The remaining sections are the reference for the markup and
the commands. The final sections describe the tool's debugging aids, including
its JSON intermediate representation.

## Requirements and build

The quickest way to obtain `specgen` is the prebuilt binary that every push to
`main` publishes as a GitHub Release. It requires glibc 2.34 or newer (Ubuntu
22.04, Debian 12, Fedora 35, RHEL 9 and their derivatives, or later), an
x86-64-v2 processor, and, for headers that include the standard library, the
host's `g++`. It requires nothing else:

```sh
curl -sSL https://github.com/steve-downey/specgen/releases/latest/download/specgen-manylinux_2_34_x86_64.tar.xz | tar -xJ
export PATH="$PWD/$(echo specgen-*-manylinux_2_34_x86_64)/bin:$PATH"
specgen --version
```

`tar -xJ` needs `xz` (`xz-utils` on Debian and Ubuntu). The same tree is also
published as `specgen-manylinux_2_34_x86_64.tar.gz`, for `tar -xz`.

The remainder of this section concerns building from source. A source build
requires GCC 16 with its C++26 libstdc++, CMake 3.30 or later, `uv`, and the
LLVM/Clang 23.1 development packages. The LLVM version is exact rather than a
minimum, because the Clang C++ API is not stable between releases.

The standard build is:

```sh
uv run cmake --preset gcc-release
uv run cmake --build --preset gcc-release
uv run ctest --preset gcc-release
```

`find_package` requests the version named by `BEMAN_SPECGEN_LLVM_VERSION`
(`23.1` by default), even when a newer LLVM is installed. An installation
outside the normal search path is named with
`-DClang_DIR=<prefix>/lib/cmake/clang`. Moving the pin with
`-DBEMAN_SPECGEN_LLVM_VERSION=<major>.<minor>` is a deliberate porting
operation, not a compatibility promise. There is no build of specgen without
the Clang front end. The repository also provides a Makefile build for daily
work: `make TOOLCHAIN=gcc-16 test`.

The preset build writes the executable to
`build/gcc-release/tools/specgen/specgen`; the Makefile build writes a
configuration-specific executable below `.build/build-gcc-16/tools/specgen/`.
Either may be invoked in place or placed on `PATH`.

`make install` installs the executable as `.install/bin/specgen`, together with
the library, headers, and CMake package, and that is the path the examples
assume. `PREFIX` selects a different prefix. Installing from the preset build
tree requires an explicit `--prefix`, because that tree is configured with
`/usr/local`.

### What `specgen` needs when it runs

`specgen` carries Clang's builtin headers (`<stddef.h>`, `<stdarg.h>`, and the
rest of Clang's resource directory) in `lib/clang/23/include`, beside the
`bin/` directory that holds the executable, and locates them relative to its
own path. The preset build tree and every installation provide that layout;
an executable moved away from it loses those headers.

`specgen` does **not** carry a C++ standard library. A header that includes
nothing, or only Clang's builtin headers, needs nothing further. A header that
includes a standard library header such as `<optional>` is parsed against the
host's libstdc++, located as `clang++` would locate it: in the newest GCC
installation on the machine. This is deliberate. A paper's wording should be
generated against the same standard library that its implementation is built
and tested with. On a machine with several GCC installations, select one in the
`--` argument tail:

```sh
specgen generate header.hpp -- --gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/16
specgen generate header.hpp -- --gcc-toolchain=/opt/gcc-16
```

Clang's search covers the distribution's own GCC (under `/usr/lib/gcc`) but not
Red Hat's GCC Toolsets under `/opt/rh`. On a RHEL-family machine whose C++
compiler is a `gcc-toolset-<N>`, name the toolset explicitly, using its own
`g++` to find the directory:

```sh
specgen generate header.hpp -- \
  --gcc-install-dir="$(dirname "$(/opt/rh/gcc-toolset-16/root/usr/bin/g++ -print-libgcc-file-name)")"
```

A release binary is stripped, so a crash report from it shows offsets rather
than function names. The same release's `specgen.debug.xz` asset restores the
names: decompress it and run `llvm-symbolizer --obj=specgen.debug <offset>`.

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

The class body supplies the synopsis, and its `\ref` comments state which
clause describes each member. The definitions supply both the clause order and
the wording attached to each declaration. specgen takes code spelling from the
source tokens; it does not reconstruct an interface by printing the AST. The
header's own namespace is dropped, and each synopsis is enclosed in
`namespace std { … }`, as the draft writes it.

Render the complete document while editing:

```sh
specgen generate widget.hpp --backend mpark --validate --no-compile-commands
```

The complete checked example is
[`tests/corpus/spec_widget.hpp`](../tests/corpus/spec_widget.hpp). For a real
library, omit `--no-compile-commands` so that specgen uses the library's
compilation database, or supply a fixed argument list after `--` that names its
include and generated-header directories.

## The authoring loop

Establish the document structure first and the entities second. This order
prevents the late discovery that clause boundaries and fragment boundaries
disagree:

1. Choose the document root. A single implementation header can be one
   document. An umbrella header can gather the public headers it includes
   inside its `.syn` region; includes outside that region remain
   implementation.
2. Write the `\rSec` outline in paper order. The shallowest sections become
   separate files under `--split`; deeper sections remain nested in their
   parent's fragment.
3. Place `\ref` group headers in class synopses, or use `\at` where source order
   does not express the intended clause.
4. Add docblocks to definitions, and mark every declaration that is deliberately
   not an ordinary specification entity with `\expos`, `\omit`, or `\merge`.
5. Run `generate --validate` frequently. Read standard error as well as the exit
   status: docblock errors are findings about usable input and do not by
   themselves stop the output.
6. Inspect all three backends before settling taste-sensitive phrasing. The
   backends share semantics, but tables, code spans, headings, and paragraph
   framing necessarily appear in different syntax.

Derivation is most useful when the implementation already states exactly what
the specification should state. In that case, prefer `\returns-expr`,
`\effects-equiv`, and derived constraints or mandates. Use authored prose when
exposing the body would reveal an implementation strategy, when the
specification is more abstract than the implementation, or when the derived
phrasing expresses the wrong contract.

## Generating wording

`generate` parses the header, builds the document, optionally validates it, and
renders it, all in one process:

```sh
specgen generate header.hpp --validate --backend latex -o wording.tex -- -Iinclude
```

Its options are:

```text
specgen generate <header> [--source <file>...]...
                 [--backend latex|mpark|org]
                 [--validate] [--paper] [--new-root <name>]
                 [--base-heading-level <n>] [--base-section-depth <n>]
                 [--split <dir>] [--root <name>]
                 [-o|--output <file>]
                 [--depfile <file>] [--dep-target <name>]
                 [--compile-commands <dir> | --no-compile-commands]
                 [-- <clang arguments>...]
```

The default backend is `latex`. The `mpark` backend emits pandoc markdown for
the mpark/wg21 framework, and the `org` backend emits org for the wg21org
exporter. `-o` and `--output` name the destination; without either, or with
`-o -`, the wording is written to standard output. With no header at all, `generate` parses a stock
snippet as a check that the front end is linked and working, reports the
result on standard error, and produces no wording; rendering options are
rejected in that form.

With either paper backend, `--paper` marks the complete fragment as added
wording and numbers its paragraphs `x`, `x+1`, and so on. The mpark backend
uses an editing-instruction `::: add` div; the org backend uses `addedblock`
around material outside any clause and `WG21_CHANGE=add` on generated clause
roots. The `latex` backend rejects `--paper`. Under `--split`, each fragment
is a document in its own right, so each carries its own framing and numbers
its paragraphs from `x`. Use `--paper` when the whole fragment is an addition;
omit it when the surrounding paper supplies that framing or when ordinary
wording blocks are wanted.

Several headers form one paper, with one document per header; see
[A paper proposing several headers](#a-paper-proposing-several-headers) below.

### Members defined in a source file

A member declared in the header and defined in a `.cpp` carries its markup on
that definition, as one defined in the header does. `--source <file>` names
such a source file, and pairs with the header named before it; it may be
repeated:

```sh
specgen generate widget.hpp --source widget.cpp --source widget_io.cpp -o wording.tex
```

Each source file is parsed as a translation unit of its own, with its own
compile flags: its compilation-database entry, or the `--` tail when one is
given. A documented definition in it of a member of a class the header
declares becomes that member's item, placed by the class's `\ref` groups or an
`\at` like any other. Everything a definition in the header supplies is read
from it: its description elements, what its body derives (*Mandates*,
`\effects-equiv`, `\returns-expr`), and its markers, so `\omit` and `\seebelow`
act on the header's synopsis too. The item declaration is still the in-class
declaration's.

A documented definition of anything else in a source file produces no wording
and is reported as an error: a namespace-scope function, a function declared
only in the source file, a class, or a member of a class this document does not
describe. A definition whose signature matches no declaration is already an
error in C++, so the source file fails to parse, and `generate` stops there
with no output. The dependency fragment `--depfile` writes includes every file
the source files' parses read.

### Fragments

`--split <dir>` writes one file per top-level section and prints an ordered
manifest of the written paths to standard output. File stems are stable names,
and extensions follow the backend: `.tex`, `.md`, or `.org`. Nodes outside every
section go into a root fragment. Its name is normally the longest common dotted
prefix of the section names; `--root <name>` supplies it when it cannot be
derived or must be overridden. `--root` is valid only with `--split`.

`--split` may be combined with `--output`, and the run then writes both views of
the render: the fragments into the directory, and the whole (for a paper, every
document joined in order) into the file. `--split` alone writes only the
fragments, and standard output receives the manifest rather than the wording.
`-o -` beside `--split` is a usage error, since the whole and the manifest
would share standard output.

Splitting does not delete files left by an earlier run. Consumers should
reconcile the output directory against the manifest, in document order.

### Where a top-level section starts

A fragment's sections must sit beneath the heading in the paper that introduces
them, and the correct level depends on how the paper is written. Two options
specify it, one per unit, because the backends do not measure the same
quantity:

- `--base-heading-level <n>` is the markdown or org heading level of a
  top-level section, for the `mpark` and `org` backends. Its default is `2`, the
  level at which the mpark/wg21 examples write their own sections. A paper that
  writes its sections at `##` and introduces the wording with a `##` heading
  requires `3`, so that the generated clauses become children of that heading
  rather than its siblings.
- `--base-section-depth <n>` is the `\rSec` depth of a top-level section, for
  the `latex` backend. Its default is `3`, the library granularity at which the
  draft itself is split.

Each option is rejected, rather than ignored, by the backend that does not
measure in its unit. Both take a whole number of at least 1; under `mpark` the
level may not exceed 6, the deepest markdown heading. The base moves only the
outermost section: nested sections still descend one step at a time from it,
and `--split` starts every fragment at the same base as a whole render.

### Proposed stable names in mpark and wg21org papers

The mpark/wg21 framework's `.sref` class looks stable names up in the current
working draft. A paper's newly proposed clauses are not yet there, so leaving
`.sref` on them produces warnings and links to pages that do not exist. Name
each proposed stable-name subtree with `--new-root`:

```sh
specgen generate include/beman/transcode/transcode.hpp --backend mpark \
  --new-root transcode --new-root null.term -- -Iinclude
```

The option applies to the `mpark` and `org` backends, is rejected by `latex`,
and may be repeated. For mpark, it removes `.sref` from each named root and from every stable name
beneath it, whether the name occurs on a heading or in a cross-reference. For
org, it makes references beneath those roots internal `CUSTOM_ID` links;
references to every other stable name link to the current working draft at
`eel.is/c++draft`. This is deliberately narrower than treating every stable
name in a paper as new wording.

### Compilation arguments

Compilation arguments come from one of these sources, in order of precedence:

1. Everything after the first bare `--` is passed to Clang verbatim. An empty
   tail supplies nothing, and the next source applies.
2. `--compile-commands <dir>` reads the header's entry from that directory's
   `compile_commands.json`. When the directory has no database, or the database
   has no entry for the header, no arguments are supplied; the search in the
   next item is not attempted.
3. Otherwise, specgen searches the directories above the header for a
   compilation database.
4. If no source supplies arguments, Clang receives only specgen's own.

specgen always passes `-std=c++2c` ahead of the supplied arguments, so a `-std`
from a database entry or the `--` tail overrides it. `--no-compile-commands` disables only the
search in the third item; it has no effect when an explicit directory or a
nonempty `--` tail already takes precedence. When a database supplies the
flags, specgen reports the selected entry's working directory on standard
error.

### Dependency files

`--depfile <file>` writes a Makefile dependency fragment that names what the
run produced and every non-system file its parse read, with an empty rule per
prerequisite so that a deleted header causes a rebuild rather than an error.
For a paper, the prerequisites of all its documents are merged, sorted, and
listed once. `--dep-target <name>` may be repeated, and replaces the target list; it serves
a build whose real target is a stamp file or an installed copy rather than a
path specgen was given. `--dep-target` is valid only with `--depfile`, and a
run writing its wording to standard output, with no `-o` or with `-o -`, needs
it: standard output is not a file a rule can name.

## Integrating generated fragments into a paper

Both `beman.transcode` and `beman.transpose` treat rendered fragments as build
artifacts with a reviewable source history. The pattern they share is:

1. Keep one script as the only place that invokes specgen. Give it an explicit
   list of document roots, root-fragment names, compiler arguments, backend
   options, and proposed stable-name roots.
2. Render with `--split` and keep the printed manifest. Directory order is not
   document order, and specgen does not remove stale files.
3. Generate into a scratch or staging directory for checks, then compare the
   result against the committed fragments. Reconcile deleted files from the
   manifest rather than from whatever an earlier run left in the output
   directory.
4. Transclude the fragments into the paper in manifest order. Edit the header
   markup, never the rendered files.
5. While the tool is under development, record the specgen revision used. A
   wording difference should be attributable to a changed header, a changed
   generator, or both.

A minimal generation step is:

```sh
specgen generate include/beman/example/example.hpp \
  --backend mpark --validate --paper --new-root example \
  --split papers/wording --root example.syn \
  -- -std=c++2c -Iinclude -I.build/generated/include \
  > papers/wording/example.manifest
```

Committed fragments allow a paper to build without the pinned LLVM toolchain
and make wording changes visible in review. `--depfile` keeps them current: it
names every non-system file the parse read, including headers reached only
through an `#include`, which are precisely the ones a hand-maintained
prerequisite list omits. With the paper's wording declared as a real file
target and the fragment `-include`d, editing a docblock rebuilds the paper, and
editing nothing rebuilds nothing:

```make
papers/wording/transpose.md: $(WORDING_HEADERS)
	specgen generate $(WORDING_HEADERS) ... --depfile papers/.deps/wording.d

-include papers/.deps/*.d
```

A dependency edge establishes only that the output is current, not that it is
correct; regeneration with `--validate` establishes the latter.

### A paper proposing several headers

One header is one specgen document, so a paper proposing several headers is
several documents, and a name specified by one of them is routinely used in the
wording of another. Rendered one header per run, `--validate` would report such
a name as foreign, because the run that uses it does not document it. The paper
is the unit that must be internally consistent, so it is rendered as one. Name
every header on a single `generate`:

```sh
specgen generate include/transpose/apply.hpp include/transpose/grade.hpp \
  --root transpose.applicative.syn --root transpose.grade.syn \
  --backend mpark --validate --paper --split papers/wording \
  -o papers/wording/transpose.md \
  --depfile papers/.deps/wording.d \
  -- -std=c++2c -Iinclude
```

Validation runs across the union of the documents' documented names, every
fragment is written to one `--split` directory, and one manifest lists the
whole paper in order. `--root`, when given at all, is repeated and pairs with
the headers in order. A paper's headers usually require it: documents that
share a stable-name prefix would otherwise derive the same root fragment name,
which specgen reports as a collision rather than allowing one to overwrite the
other.

`-o` beside `--split` requests both views of the render: the pieces, for
`\input`, and the whole, with every document joined in order, for comparison
against the working draft. Both come from one parse, and the whole contains
the pieces in manifest order, so their wording cannot disagree; only the
per-fragment framing described under `--paper` differs. With several headers,
`generate` parses all of them before writing anything and stops at the first
that fails, so a paper is never left half written.

Two document layouts are common:

- A library with one specification-facing header per proposed header can place
  each header's `.syn` fence and clauses in that file, then name every header on
  one specgen run. Give each an explicit `--root` so that their loose synopsis
  nodes cannot collide.
- An umbrella header can place its public component includes inside one
  gathered `.syn` region and its clause outline after the fence. specgen follows
  those includes as part of one document. The implementation can then remain
  divided across ordinary component headers without merging source files for
  the generator.

## Comment forms and document structure

Comment spelling is significant:

- `//!` and `/*! ... */` are specgen docblocks. Their contents are parsed as
  elements and structural markers.
- `///` and `/** ... */` are Doxygen. They are removed from synopses and
  extracted bodies, and are never promoted into specification wording.
- Plain `//` and `/* ... */` comments are draft-form comments. They are retained
  wherever the corresponding source is rendered.

Clang treats an unbroken run of comment lines as one comment, so a draft-form
line in the same run as a Doxygen line is removed with it. A blank line between
the two keeps the draft-form comment.

A header documented only with Doxygen is valid input and can produce empty
wording. Validation then reports its class members as undocumented. Those
coverage errors are intentional: successful parsing must not silently
reinterpret Doxygen prose as specification. Coverage reads class bodies only,
so a Doxygen-only header with no class validates clean, with empty output.

Draft-form section markers build the document tree:

```cpp
// \rSec3[optional.ctor]{Constructors}
```

The depth determines nesting, the bracketed value is the stable name, and the
braced value is the title. A numbered draft-style heading that ends in a stable
name, such as `// 22.5.3.3 Destructor[optional.dtor]`, is insufficient because
it states no depth; specgen warns and suggests the `\rSec<depth>[stable]{title}`
form. A title too long for one line may continue on the immediately following
plain `//` lines, which is the shape `clang-format` produces when it wraps a
long marker; the continuation lines are joined into one title with single
spaces. A `{title}` that never closes is reported as a malformed marker.

An exact Doxygen fence closes an ordinary section explicitly:

```cpp
// \rSec2[optional.bad]{Bad optional access}
// ... declarations in the clause ...
/// END [optional.bad]

// declarations here return to the document root
```

The fence is useful for exposition-only helpers written between clauses: under
`--split`, they enter the root fragment instead of the preceding clause's
fragment. The stable name must match the currently open section. A mismatch,
or an END fence when no section is open, is a warning and does not change
placement. Closing a nested section resumes its parent; closing a top-level
section resumes the document root. The same spelling bounds a gathered `.syn`
region, as described below; that fence is consumed by the gatherer.

Inside a class, a draft-form group header routes the documented in-class
members that follow it to the section with the matching stable name:

```cpp
// \ref{optional.ctor}, constructors
```

An explicit `\at stable.name` marker on a member overrides this inferred route.

A gathered header synopsis can span files. The declarations of every header
`#include`d **inside** the region are part of the document, in the order in
which their includes appear, so an umbrella header specifies the whole of what
it includes there:

```cpp
// \rSec2[transcode.syn]{Header `<transcode>` synopsis}
#include <beman/transcode/error.hpp>
#include <beman/transcode/concepts.hpp>
/// END [transcode.syn]

#include <beman/transcode/detail/labels.hpp>   // outside: implementation

// \rSec2[transcode.errors]{Error types}
```

An include outside the region remains invisible to the document. A followed
header carries no `\rSec` markers of its own; its clauses are the umbrella's,
after the fence, and its declarations reach them by routing.

Routing within a gathered header synopsis works the same way. A documented
declaration folded into the region contributes its declaration to the synopsis,
and its description goes to the section that `\at` names, or to the section
named by the `\ref` group header above it. With neither, the description is
rendered beside the synopsis, in the section that contains the region; this is
how a range adaptor object receives a clause of its own without leaving the
header synopsis to which it belongs. A route that names no existing section is
a validation error, as it is on a class member.

An in-class member definition is reduced to a declaration in the synopsis,
since a body is never synopsis content, and the member is described in its own
subclause like any other. Declaring members in the class and defining them out
of line remains the recommended style, because the definition's lexical
position determines where its wording is placed, but it is not required for a
clean synopsis.

The docblock of a class or class-template definition describes the *type*. Its
description elements are rendered immediately after the class synopsis, with no
item declaration of their own, which is the shape the draft uses for a class's
general subclause. An authored `\mandates` there replaces the paragraph derived
from the class's direct `static_assert`s. A `\at stable.name` on the class
definition routes both that derived paragraph and the class's own description
to the named section, in that order; this is particularly useful for a class
gathered from a component header whose `\rSec` outline is in an umbrella header.
A route to a section that does not exist is a validation error.
`\verbatim-itemdecl` and the `*-equiv` extraction markers are errors on a class
definition: the former has `\verbatim-synopsis` as its class-level
counterpart, and the latter require a function body to extract.

A docblock documents the declaration that follows it. Wording comes from class
and class-template definitions (a synopsis, the class's own description, and
routed members), from documented function *definitions*, from documented
in-class type aliases and static data members, and, at namespace scope, from documented aliases, alias
templates, variables, variable templates, concepts, enumerations, and record
declarations that the header never defines (an undefined class-template primary
renders as its own declaration). A documented enumeration's item declaration is
the enumeration as written, enumerator list included, and its description
states what the enumerators mean. A docblock on any other kind of entity, or on
a namespace-scope function declaration rather than its definition, is reported
as an error, because it would otherwise produce no wording and do so silently.
In a class body, a member's markup is read from its definition. A docblock on
the declaration of a member defined out of line is reported as an error that
names the definition, as is one on a member whose definition is not in the
translation unit at all, such as one defined in a source file not named with
[`--source`](#members-defined-in-a-source-file). Three kinds of
member have no other place for their markup, so their declarations carry it: a
pure virtual member with no definition, and `= default` and `= delete` members.
Each gets an item declaration, `= 0` or its other tail included, when its
docblock has a description element or `\describe`. A documented static data
member gets an item of its own, as the draft gives
`atomic<T>::is_always_lock_free` one: its declaration, initializer included,
followed by its elements, placed by its class's `\ref` group or an `\at` the
way an in-class alias is. A docblock on a non-static data member, or on an
exposition-only static one, is reported as an error, because such a member has
no item of its own: its declaration is its specification, and its class's
description says what it means. In every one of these places, a docblock holding only markers that act on
the declaration (`\omit`, `\merge`, `\elsewhere`, `\expos`, `\seebelow`,
`\freestanding`) is not an error.

## Description elements

A specgen docblock contains only the elements that apply. The complete element
vocabulary, in canonical output order, is:

```text
\constraints  \mandates  \expects  \hardexpects  \effects  \sync
\ensures      \result    \returns  \throws       \complexity
\remarks      \errors
```

The display labels include *Preconditions* for `\expects`, *Hardened
preconditions* for `\hardexpects`, *Postconditions* for `\ensures`, and *Error
conditions* for `\errors`. Authored order does not determine output order: an
element out of order produces a note, and the output is put in canonical order.
A duplicated element produces a warning and is retained. An unknown tag is an
error.

Text after a tag begins the element's first paragraph. A blank decorated line
separates paragraphs, and the next tag ends the current element. Backticks mark
code. Use `\iref{stable.name}` outside backticks for a reference in prose; it
may name a standard subclause outside the generated document.

An element may end in one authored itemization:

```cpp
//! \constraints All of the following are true:
//! \item `T` is complete, and
//! \item `T` meets the requirements in \iref{some.requirements}.
```

Each nonblank line that follows an item continues that item. After the first
`\item`, only another `\item` or a new element may follow; no further prose can
be placed after the list within the same element.

An element may instead end in one two-column table:

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
and exactly two `\cell` entries per row. Lines without a tag continue the
active caption, column heading, row heading, or cell. `\endlib2dtab2` is
required, and the table ends its element.

`\libtab2` produces a flat two-column table, such as an enumeration's meanings.
Here the text on `\row` is the first column, and each row has one `\cell` for
the second:

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
first data cell, followed by one `\cell`. Both require two column headings and
at least one row, and both end their element.

## Extraction markers

- `\effects-equiv` extracts a function body as an *Effects: Equivalent to:*
  block. It removes consumed leading `static_assert`s, inactive conditional
  branches, preprocessing directive lines, specgen comments, and Doxygen
  comments. Draft-form comments within the body remain; extraction begins at
  the first statement, so a comment before it is not included.
- `\returns-expr` renders *Returns*: `E` from a function body that is exactly one
  `return E;`, after any consumed leading `static_assert`s, with `E` rewritten
  like extracted code. A *Returns* names a value and no effects, so any other
  body is an error: use `\effects-equiv` when the body does more than return one
  expression. The marker's old name, `\returns-equiv`, is an error that names
  `\returns-expr`.
- `\constraints-in-decl` retains the requires-clause in the item declaration
  instead of deriving a separate *Constraints:* element.

`\effects-equiv` and an authored `\effects` are mutually exclusive, as are
`\returns-expr` and an authored `\returns`.

By default, associated constraints are removed from the item declaration and
rendered as derived *Constraints*. A leading run of body-local type aliases
followed by `static_assert`s produces derived *Mandates*; the aliases remain in
an extracted equivalent-to body, and the consumed assertions do not. An
authored `\constraints` or `\mandates` replaces the corresponding derived
wording, and the derivation is retained as evidence for the drift checks.

## Placement and grouping markers

- `\omit` excludes a declaration entirely.
- `\elsewhere` excludes a declaration from generated wording and records that
  the paper declares the name by hand elsewhere. When several headers are
  rendered together, the name is visible to validation in the sibling
  documents. Use it instead of `\omit` when generated wording legitimately
  names such a hand-authored declaration. It is mutually exclusive with
  `\omit`, `\merge`, and `\expos`.
- `\merge` suppresses a declaration that another specification entity
  represents. It removes the marked declaration from the synopsis and the
  wording; on a namespace-scope record definition, it suppresses the record's
  entire contribution. The usual case is a conditionally trivial special
  member: a defaulted twin marked `\merge`, constrained to the cases in which
  `T`'s member is trivial, and a user-provided survivor that carries the
  wording. The conjunct of the survivor's `requires`-clause that serves only to
  distinguish it from the twin (its `!X` against the twin's `X`) is dropped from
  the synopsis, the item declaration, and the derived *Constraints*, since the
  pair is one entity: `requires copy_constructible<T> &&
  (!is_trivially_copy_constructible_v<T>)` renders as `requires
  copy_constructible<T>`. This applies to a trailing clause written as a flat
  `&&` chain; any other shape is left whole.
- `\describe` forces an item declaration for a documented `= default` or
  `= delete` entity.
- A bare `\also`, or an empty docblock, joins an overload's signature to the
  preceding described item in the same section.
- `\group id` names a group primary. `\also id` joins that earlier primary in
  the same section when the declarations are not adjacent. Targets are resolved
  from left to right, so forward and cross-section targets are invalid.
- `\at stable.name` routes an in-class documented member, a folded-in namespace
  entity, or a class's own wording to the named section, overriding the
  inferred placement. On a class, it carries both the class-scope mandates
  paragraph and the class description.

An `\also` block should contain no description elements. `\group` and `\also`
are mutually exclusive in the same docblock.

## Synopsis and spelling markers

specgen normalizes the spellings about which the draft is strict, so a header
may follow its own house style. `T const&` and `const T&` both render as
`const T&`, and `T *p` renders as `T* p`; whichever the library writes, the
wording follows [structure.specifications].

- `\expos` marks an entity as exposition-only. The default spelling removes trailing
  underscores and changes the remaining underscores to hyphens; `\expos(name)`
  supplies the exact exposition name. Namespace-scope concepts, variable
  templates, variables, enumerations, aliases, alias templates, classes, and
  class templates may be marked, as may class members. The uses of an exposition-only
  enumeration, both its type and a qualified enumerator, also render under the
  exposition name. A **nested class** may be marked as well; with a bare
  `\seebelow` it renders as a declaration, `class $iterator$; // exposition
  only`, which is how the draft writes a view's iterator. Its own members are
  exposition-only with it, so an extracted body that names one uses the
  exposition name.

  A partial or explicit specialization follows its primary and needs no marker
  of its own: it is the same entity and renders under the same exposition name.
  The marked declaration may reside in an included implementation header
  outside the document's extent; uses in the specified document still render as
  `\exposid`, so moving machinery into a `detail/` header does not force it back
  into the public header. The marker does not bring that declaration into the
  document; only the uses that the document reaches are rewritten. On a
  namespace-scope `using` declaration, `\expos` marks the introduced name and
  its target without emitting the `using` line itself; uses through the
  declaration render under the exposition name, whatever kind of entity the
  target is, and an enumerator of an exposition-only enumeration reached that
  way is not reported as foreign.

  A forward declaration of a marked class or class template is reproduced as
  an exposition-only declaration, `struct $token$; // exposition only`, ahead
  of the definition. Such a declaration is usually there because the synopsis
  needs it to compile, which is what marking the class suggests. A public component header
  included inside a gathered `.syn` region differs: it is part of the
  document's extent, so its declarations contribute to that synopsis.
- A bare `\seebelow` masks a function's return type: a leading return type
  entirely, and an explicit trailing return type as `auto f(...) -> see below;`,
  preserving the trailing form. `\seebelow noexcept` and `\seebelow explicit`
  mask only the condition inside that specifier. On a documented in-class type
  alias, or a documented namespace-scope alias, a bare `\seebelow` masks the
  complete right-hand side.

  On a documented namespace-scope variable or variable template, a bare
  `\seebelow` renders the declared type as *unspecified* and drops the
  initializer, which is the customization-point-object form,
  `inline constexpr unspecified name;`. On a documented static data member, a
  bare `\seebelow` drops only the initializer, and only from the item
  declaration, as the draft writes `static constexpr bool is_specialized;` for
  a member whose synopsis declares it `= false`. Combined with `\expos`, a
  namespace variable's two masks compose:
  `inline constexpr unspecified $name$; // exposition only`. The targeted forms
  do not apply to a variable or a static data member, and using one is an
  error. On a documented
  namespace-scope concept, a bare `\seebelow` masks the constraint-expression,
  as it masks an alias's right-hand side. Both compose with `\expos`: an alias
  renders as `using $name$ = see below; // exposition only`, and a concept as
  `template<class T> concept $name$ = see below; // exposition only`.

  An enumeration accepts no form of `\seebelow`: the draft spells neither an
  enum-base nor an enumerator list as *see below*, and a marker that silently
  had no effect would leave a description that contradicts the synopsis beside
  it.
- `\impdef` masks the right-hand side of a documented type alias, in a class or
  at namespace scope, as *implementation-defined*. It applies only to aliases and is mutually exclusive
  with `\seebelow`.
- `\freestanding` and `\freestanding-deleted` add the corresponding literal
  draft comment to a function declaration in the class synopsis.
- `\verbatim-synopsis` is terminal. All following decorated lines form one
  exact synopsis, free of spans, without C++ parsing or formatting. Written as a
  class definition's own docblock (with no blank line before the class), the
  authored text *replaces* the extracted synopsis, while the class's members,
  roster, and routing are processed as usual, including the
  `namespace std {` enclosure that every synopsis receives. Detached, the block
  stands alone as an anonymous synopsis and is left exactly as written, so it
  must supply its own namespace.
- `\verbatim-itemdecl` is terminal. Authored elements before it remain the item
  description; all following decorated lines form one exact item declaration,
  free of spans. Multiple declaration lines are not split, parsed, formatted,
  indexed, or interpreted as draft markup. Written as a declaration's own
  docblock, the authored text *replaces* that declaration's extracted item
  declaration, so the item appears once. A detached block stands alone, and is
  paired with `\omit` or `\merge` on the real declaration when one exists, as
  in the hash-specialization pattern.

`\verbatim-synopsis` written as the docblock of anything other than a class or
class-template definition is an error.

## Validation and diagnostics

Run `generate --validate` as part of the normal authoring loop. Validation
checks:

- Span and authored table structure.
- Coverage: each declaration in a class body must be described,
  exposition-only, defaulted or deleted, explicitly omitted, or represented by
  a merged declaration, and routed wording must name a section that exists.
  This holds for a class whose synopsis is folded into a gathered header
  synopsis too. A *public data member* requires none of these: its
  declaration is its specification, as in the draft's `from_chars_result`, and
  the class's own description may name it. Namespace-scope declarations are
  outside coverage, including those in a gathered header synopsis. A
  namespace-scope function, variable or alias produces wording only when it is
  documented, and an undocumented one is treated as an implementation helper,
  with no finding.
- Leakage: wording, item declarations, equivalent-to bodies, tables, and
  synopses must not name invisible members, surviving implementation namespace
  qualifiers, or bare names that resolve to declarations outside the document.
  A declaration marked `\expos` may reside in an included implementation
  header; otherwise, restate the contract in documented terms or use authored
  prose. A foreign helper and a declaration in the document may legitimately
  share a spelling, such as an enumerator and a generated lookup table; the
  document's declaration prevails, so that collision alone is not reported as
  leakage. Across the several documents of one paper, the names a sibling
  generates, and those it explicitly promises through `\elsewhere` and
  exposition-only `using` declarations, join the same paper-wide visibility
  set.
- Local references in synopsis code must name generated sections. Prose `\iref`
  references may deliberately name external standard subclauses.
- Authored *Constraints* and *Mandates* must neither duplicate nor contradict
  the derived conjuncts they suppress, and a derived conjunct with no authored
  counterpart anywhere in the description is reported as a warning.
- An authored *Throws:* paragraph must not contradict an unconditionally
  `noexcept` signature, and *Throws:* Nothing on such a signature is reported
  as redundant. There is deliberately no check in the reverse direction.
- A synopsis must contain code, and an item must have a declaration or a
  description.
- In a class that already marks members exposition-only, unmarked private data
  is noted as a possibly missing `\expos` marker.
- A hidden helper function used only by an unextracted body is noted, unless a
  rendered fragment already exposes the same name, in which case the leakage
  check reports the error.

Docblock and source-structure diagnostics are printed by `generate` as
`<header>:<line>: <severity>: <message>`. They include element-order notes,
duplicate-element warnings, unknown-tag errors, malformed `\rSec` markers,
unrecognized draft-style headings, gathered `.syn` regions that are not closed
before the next section, and errors for a docblock on an entity kind
that produces no wording (a namespace alias, or a function declaration whose
markup belongs on the definition). These diagnostics describe markup on a header
that parsed successfully, so even an error does not prevent output or change
`generate`'s exit status. Always inspect standard error.

`--validate` prints findings as `specgen: <context>: <severity>: <message>`.
After notes and warnings, rendering continues. If any finding has error
severity, every finding is printed, rendering is skipped, and the command exits
with status 1.

## Debugging aids

### The declaration interleave

`dump-decls` prints the sequence of declarations and comments in the main file
from which the front end builds the document. It is the first tool to consult
when a section is unexpectedly empty or a docblock attaches to the wrong
declaration:

```sh
specgen dump-decls header.hpp -- -Iinclude
specgen dump-decls header.hpp --compile-commands build
```

It accepts the same compilation-database options and `--` tail as `generate`.
Because it is a debugging command, it does not reject a header on which Clang
reports a parse error: it warns and prints the possibly incomplete interleave.
It reads exactly one header: naming several is a usage error, and a header
that cannot be read is an error.

### The JSON intermediate representation

Internally, `generate` lowers the parsed header to a JSON intermediate
representation (IR) and renders that. The IR is the boundary between parsing
and presentation, and the test suite relies on it heavily; for authoring a
paper it is seldom needed. It is useful for examining exactly what the front
end extracted, and for rendering on a machine that has no Clang headers to
parse against, since rendering IR invokes no compiler.

`generate --emit-ir` writes the document as IR instead of rendering it, and
`render --from-ir` renders IR:

```sh
specgen generate --emit-ir header.hpp -o wording.json -- -Iinclude
specgen render --from-ir wording.json --validate --backend latex -o wording.tex
```

Standard input connects the two without a temporary file:

```sh
specgen generate --emit-ir header.hpp -- -Iinclude |
  specgen render --from-ir - --validate --backend latex
```

Both routes produce the same wording as the single `generate` command, byte for
byte. The golden suite's `.singlepass` cases verify this with the `latex`
backend for every golden case generated from a corpus header.

`--emit-ir` stops before validation and the backends, so it rejects
`--validate` and every option that steers a backend, including an explicit
`--backend`, rather than ignoring them. It accepts `-o`, `--depfile`,
`--dep-target`, and the compilation-database options. The IR format holds one document per file,
so `--emit-ir` takes exactly one header.

`render` accepts these options:

```text
specgen render --from-ir <file|-> [--from-ir <file>...]
               [--backend latex|mpark|org]
               [--validate]
               [--paper] [--new-root <name>]
               [--base-heading-level <n>] [--base-section-depth <n>]
               [-o|--output <file>]
               [--split <dir>] [--root <name>...]
               [--depfile <file>] [--dep-target <name>]
```

Each has the meaning it has for `generate`. `--from-ir` may be repeated, once
per document of one paper, which `render` then treats as `generate` treats
several headers: `--validate` runs across the union of the documents'
documented names, and `--root`, when given at all, is repeated and pairs with
each `--from-ir` in order. `render --depfile` names the IR files the render
read as its prerequisites; `--from-ir -` reads standard input, which is not a
file, so it is left out.

## Exit behavior

- Successful `generate`, `render`, and `dump-decls` invocations exit with
  status 0.
- Command-line usage errors exit with status 2, as do an unknown command and a
  missing one; both print the usage summary on standard error. `--help` prints
  it on standard output and exits with status 0.
- `generate` exits with status 1 when a header cannot be read, Clang cannot
  build an AST, Clang reports a parse error, or the output cannot be written.
  It never emits plausible partial wording after a C++ parse failure. When it
  renders, it also exits with status 1 on a fragment error or, under
  `--validate`, on an error-severity finding. With several headers, it parses
  all of them before writing anything and stops at the first that fails.
- `render` exits with status 1 on unreadable or invalid JSON, an output
  failure, a fragment error, or an error-severity validation finding.
- A `--depfile` that cannot be written causes exit status 1. It is written
  after every output it names, so a run that reaches that point has already
  produced the wording. `--depfile` with nothing to name as a target, and
  `--dep-target` without `--depfile`, are usage errors and exit with status 2.
- `dump-decls` is diagnostic by design: after a recoverable Clang parse error
  it warns, prints the partial interleave, and exits with status 0. It exits
  with status 1 when the header cannot be read or Clang cannot build an AST,
  and with status 2 when more than one header is named.

`specgen --help` prints the current command-line summary, and
`specgen --version` prints the installed version and source revision as
`specgen 0.5.0 (git <commit>)`. A source archive without Git metadata can set
`BEMAN_SPECGEN_GIT_COMMIT` explicitly when it is configured; otherwise the
revision is reported as `unknown`.
