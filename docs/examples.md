- [CLI Coverage](#cli-coverage)
- [General Command Information](#general-command-information)
- [Minimal Header](#minimal-header)
- [The Three Backends](#the-three-backends)
- [Paper Mode](#paper-mode)
- [The Wording Diff](#the-wording-diff)
  - [Against the working draft](#the-wording-diff-draft)
- [Fragment Output](#fragment-output)
- [A Paper Of Several Headers](#a-paper-of-several-headers)
- [Validation](#validation)
- [Explicit Include Path](#explicit-include-path)
- [Declaration Interleave](#declaration-interleave)
- [Converting a Header](#converting-a-header)
- [Larger Header Example](#larger-header-example)

<!-- markdownlint-disable -->

Every command below is a script in [`examples/cli/`](../examples/cli), and every output below is what that script printed. Both are transcluded from the repository, so neither is retyped here, and neither can drift from the other without a test detecting it.

Running the scripts requires the binary. `make install-release` installs it at `.install/bin/specgen`, which is where the scripts look for it; `SPECGEN` names another location, such as a build tree. Every build of specgen includes the Clang front end, so every command here works with any binary.

```sh
make install-release          # or: export SPECGEN=<build tree>/tools/specgen/specgen
examples/cli/run-all.sh       # re-capture everything shown below
```

`run-all.sh` rewrites the checked-in output, and `ctest -R examples.` checks it. Most of those checks compare a captured file against the golden suite; the remainder re-run a script and compare its output. The output shown here is therefore not an illustration of what specgen does, but a record of what it did.

The ordinary way to use specgen is a single `generate` command, which parses a header and renders its wording in one process. Several examples instead use `generate --emit-ir` and `render --from-ir`, which write and read specgen's JSON intermediate representation. That route is a debugging aid rather than an authoring workflow, but it lets an example render a fixed, checked-in IR fixture from the golden suite, so that only the option under demonstration varies.


<a id="cli-coverage"></a>

# CLI Coverage

| Command or option                                      | Example section                       |
|------------------------------------------------------ |------------------------------------- |
| `--help`                                               | General command information           |
| `generate <header>`                                    | Minimal header                        |
| `generate --emit-ir`                                   | Minimal header                        |
| `--no-compile-commands`                                | Minimal header                        |
| `-o`, `--output`                                       | Minimal header, Larger header example |
| `-- <clang args>...`                                   | Explicit include path                 |
| `--compile-commands <dir>`                             | Explicit include path                 |
| `dump-decls`                                           | Declaration interleave                |
| `split_inline <header>`                                | Converting a header                   |
| `split_inline --move-comments`                         | Converting a header                   |
| `render --from-ir <file>`                              | The three backends                    |
| `render --from-ir -`                                   | Minimal header                        |
| `--backend latex`, `mpark`, `org`                      | The three backends                    |
| `--validate`                                           | Validation                            |
| `--paper`                                              | Paper mode                            |
| `--baseline-header`, `--baseline-ir`, `--diff-context` | The wording diff                      |
| `--split`, `--root`                                    | Fragment output                       |
| `generate <header>...` (a paper)                       | A paper of several headers            |
| `-o` with `--split`                                    | A paper of several headers            |
| `--depfile`, `--dep-target`                            | A paper of several headers            |


<a id="general-command-information"></a>

# General Command Information

```sh
#!/bin/sh
# examples/cli/10-help.sh                                             -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# General command information.
. "$(dirname -- "$0")/env.sh"

OUT=$(out_dir 10-help)
cd "$REPO_ROOT"

"$SPECGEN" --help > "$OUT/specgen-help.txt"
"$SPECGEN" generate --help > "$OUT/generate-help.txt"
"$SPECGEN" render --help > "$OUT/render-help.txt"
"$SPECGEN" dump-decls --help > "$OUT/dump-decls-help.txt"
"$SPECGEN" import --help > "$OUT/import-help.txt"
```

Every command accepts `--help`, and all four invocations print the same usage text, so only one is shown here.

```text
usage: specgen <command> [options]

commands:
  render      render IR JSON to wording fragments
  generate    render a C++ header to wording fragments
  import      read subclauses of the working draft's LaTeX into IR JSON
  dump-decls  print a header's decl/comment interleave (debug)

import options:
  <file.tex>                the draft source file holding the subclauses
                             (exactly one)
  --section <stable-name>   a subclause to import, with its subsections; the
                             result has one top-level section per use, in the
                             order given. Required, and may be repeated
  -o, --output <file>       write the IR here instead of to standard output

  A construct the IR has no kind for is kept as raw LaTeX and reported as a
  warning naming it and the subclause. A name the file lacks is an error.

dump-decls options:
  <header>                  the header to parse (exactly one)
  --compile-commands <dir>  read compile flags for <header> from <dir>'s
                             compile_commands.json
  --no-compile-commands     suppress the search for a compile_commands.json
                             above <header>, which is otherwise done by
                             default (no effect alongside --compile-commands
                             or a `--` tail; both already outrank the search)
  -- <clang args>...        pass these to Clang verbatim instead of consulting
                             any compile_commands.json; everything after the
                             first bare `--` is taken as-is, with no further
                             option parsing

generate options:
  <header>...                the headers to parse, one document each and all
                             of them one paper, in the order named; with none,
                             parse a stock snippet as a link-proving smoke
                             check
  --synopsis-parameter-names keep|drop
                             with drop, a synopsis declares a function's
                             parameters without their names, as the draft's
                             synopses mostly do; an item declaration keeps
                             them. A name the declaration uses again, in a
                             noexcept or a requires-clause, stays. Default keep
  --keep-synopsis-comments  keep every plain comment of a class body in its
                             synopsis. By default a synopsis keeps a
                             `// \ref{x}, title` group header and a comment
                             that starts `\syn`, printed without the marker,
                             and drops the rest as implementation comments
  --source <file>           a source file defining members the preceding
                             header declares: a documented definition in it
                             supplies that member's wording, as one in the
                             header would. Parsed as its own translation unit,
                             with its own compile flags; may be repeated
  --emit-ir                 emit the document tree (design §3.2) as IR JSON
                             for a later `render --from-ir`, instead of
                             rendering wording here. One document per file is
                             what the format is, so this takes one header
  --backend <name>          latex (default), mpark, or org
  --validate                run the wording validators before rendering; a
                             finding at error severity aborts the render
                             (exit 1) instead. An error found reading the
                             header's docblocks fails the run with or without
                             it, after the wording is written
  --warnings-as-errors      with --validate: a finding at warning severity
                             fails the run too, in the same way. A warning is
                             something specgen verified; a note is something
                             to check outside it, and never fails the run
  --paper                   mark the whole fragment as added wording: the
                             editing-instruction form of each backend, with
                             paragraphs numbered as added under mpark and org
  --new-root <name>         treat <name> and every stable name beneath it as
                             a clause proposed by this paper (mpark and org),
                             rather than one already in the working draft. May be
                             repeated; references beneath any root use a local
                             target
  --baseline-ir <file>      the wording diff: render what changed since this
                             saved IR, marked in the backend's editing form.
                             May be repeated, one per document in order;
                             implies --paper
  --baseline-header <path>  the wording diff against this header's wording,
                             parsed with the same compile flags as the header
                             it pairs with, in order. May be repeated
  --baseline-rev <rev>      the wording diff against the headers as they were
                             at this git revision of their repository, checked
                             out into a scratch tree for the run; a header the
                             revision lacks is new wording. One per run
  --baseline-draft <dir>    the wording diff against the C++ working draft:
                             the top-level sections the headers generate are
                             read, by stable name, from the *.tex files in
                             this draft source directory; a section it lacks
                             is new wording. One per run
  --inherit-draft <dir>     resolve \asdraft against the draft in this
                             directory, as --baseline-draft does, but render
                             the wording itself and not a diff. One per run
  --apply-to-draft <dir>    resolve \asdraft against the draft in this
                             directory, and write its source files with the
                             paper's changes made in them, each changed file
                             to the directory -o names; a change that cannot
                             be placed is an error. One per run
  --diff-context <level>    with a baseline: `changed` (the default) keeps the
                             sections that changed, `all` keeps every section,
                             `items` also elides the unchanged items inside a
                             changed section; `decls` also elides an unchanged
                             description inside a changed item
  --base-heading-level <n>  heading level of a top-level section; nested
                             sections descend from it (mpark and org only;
                             2 by default). One to six under mpark, markdown's
                             deepest heading; org has no upper limit
  --base-section-depth <n>  \rSec depth of a top-level section; nested sections
                             descend from it (latex only; 3 by default, the
                             draft's library split granularity)
  --section-number <name>=<number>
                            give a top-level section its number in the draft
                             (latex only): `\clausenumber{<number>}` is written
                             ahead of its \rSec, so a fragment's headings and
                             references print 22.10.17.4 and not the draft
                             macros' unknown-number glyph. May be repeated
  --split <dir>             write one fragment per top-level section into <dir>,
                             named from its stable name (optional.ctor.tex), and
                             list the paths written on standard output
  --root <name>             name the fragment holding the nodes outside every
                             section (--split only); derived from the sections'
                             common stable-name prefix when omitted. With
                             several headers, repeats to name each document's
                             root fragment, pairing in order
  -o, --output <file>       write the whole here instead of standard output
                             ("-" is standard output); alongside --split, write
                             both the whole and the fragments
  --depfile <file>          write a Makefile dependency fragment naming what
                             this run produced and every file its parse read,
                             with an empty rule per prerequisite so a deleted
                             header rebuilds rather than erroring. System
                             headers are left out
  --dep-target <name>       name this target in the dependency fragment
                             instead of the files actually written; repeats
                             (--depfile only)
  --compile-commands <dir>  read compile flags for <header> from <dir>'s
                             compile_commands.json
  --no-compile-commands     suppress the search for a compile_commands.json
                             above <header>, which is otherwise done by
                             default (no effect alongside --compile-commands
                             or a `--` tail; both already outrank the search)
  -- <clang args>...        pass these to Clang verbatim instead of consulting
                             any compile_commands.json; everything after the
                             first bare `--` is taken as-is, with no further
                             option parsing

render options:
  --from-ir <file>          IR JSON to read; "-" for standard input (required).
                             May be repeated, once per document of one paper:
                             --validate then runs across the union of their
                             documented names, so a name specified by a
                             sibling document is not foreign. Several inputs
                             render as one paper -- joined in order for a
                             single output, split into per-clause fragments,
                             or both -- and --root, when given at all, repeats
                             too, pairing with each --from-ir in order
  --backend <name>          latex (default), mpark, or org
  --validate                run the wording validators before rendering; a
                             finding at error severity aborts the render
                             (exit 1) instead
  --warnings-as-errors      with --validate: a finding at warning severity
                             aborts the render too. A warning is something
                             specgen verified; a note is something to check
                             outside it, and never fails the run
  --paper                   mark the whole fragment as added wording: the
                             editing-instruction form of each backend, with
                             paragraphs numbered as added under mpark and org
  --new-root <name>         treat <name> and every stable name beneath it as
                             a clause proposed by this paper (mpark and org),
                             rather than one already in the working draft. May be
                             repeated; references beneath any root use a local
                             target
  --baseline-ir <file>      the wording diff: render what changed since this
                             saved IR, marked in the backend's editing form.
                             May be repeated, one per document in order;
                             implies --paper
  --diff-context <level>    with a baseline: `changed` (the default) keeps the
                             sections that changed, `all` keeps every section,
                             `items` also elides the unchanged items inside a
                             changed section; `decls` also elides an unchanged
                             description inside a changed item
  --base-heading-level <n>  heading level of a top-level section; nested
                             sections descend from it (mpark and org only;
                             2 by default). One to six under mpark, markdown's
                             deepest heading; org has no upper limit
  --base-section-depth <n>  \rSec depth of a top-level section; nested sections
                             descend from it (latex only; 3 by default, the
                             draft's library split granularity)
  --section-number <name>=<number>
                            give a top-level section its number in the draft
                             (latex only): `\clausenumber{<number>}` is written
                             ahead of its \rSec, so a fragment's headings and
                             references print 22.10.17.4 and not the draft
                             macros' unknown-number glyph. May be repeated
  --split <dir>             write one fragment per top-level section into <dir>,
                             named from its stable name (optional.ctor.tex), and
                             list the paths written on standard output
  --root <name>             name the fragment holding the nodes outside every
                             section (--split only); derived from the sections'
                             common stable-name prefix when omitted. With
                             several --from-ir inputs, repeats to name each
                             document's root fragment, pairing in order
  -o, --output <file>       write the whole here instead of standard output
                             ("-" is standard output); alongside --split, write
                             both the whole and the fragments
  --depfile <file>          write a Makefile dependency fragment naming what
                             this run produced and the IR files it read, with
                             an empty rule per prerequisite. Standard input is
                             not a file, so it is left out
  --dep-target <name>       name this target in the dependency fragment
                             instead of the files actually written; repeats
                             (--depfile only)

general:
  -h, --help                show this message
  --version                 show the version
```


<a id="minimal-header"></a>

# Minimal Header

The smallest useful header is `spec_widget.hpp`. It has class synopsis groups, two sections, attached out-of-line definitions, and ordinary description elements.

```cpp
// tests/corpus/spec_widget.hpp                                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (decision hermetic-corpus). A Beman-style shape
// here: a declaration-only class with two `\ref` group comments in its body
// (constructors, observers), two constructors and one observer declared
// in-class, and a definition region holding two sibling `\rSec3` sections —
// one per group — each with its out-of-line member definitions and a small
// `//!` docblock. Just enough surface to exercise build_document() folding
// `\rSec` comments into nested ir::Section siblings with decls hanging under
// them (design §3.2), without any nesting depth to worry about.
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_WIDGET_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_WIDGET_HPP

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

//! \effects None.
//! \returns `true` if the widget holds no value, `false` otherwise.
bool widget::empty() const { return value_ == 0; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_WIDGET_HPP
```

```sh
#!/bin/sh
# examples/cli/20-widget-ir.sh                                        -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The smallest useful header, spec_widget.hpp: to IR, to wording in one pass,
# and to the same wording the two-process way with the IR on a pipe.
#
# `--no-compile-commands` is not decoration: the
# repository keeps a gitignored compile_commands.json symlink at its root and
# `generate` probes for one by default, so without this the parse would depend
# on whether a build happens to be configured. It is also exactly what the
# golden harness passes, which is what lets widget.json be compared against
# golden.widget_skeleton's expected.json byte for byte.
. "$(dirname -- "$0")/env.sh"

OUT=$(out_dir 20-widget-ir)
cd "$REPO_ROOT"

"$SPECGEN" generate --emit-ir tests/corpus/spec_widget.hpp \
    --no-compile-commands \
    -o "$OUT/widget.json"

# Header to wording in a single pass: the IR stays inside the process and is
# never written down.
"$SPECGEN" generate tests/corpus/spec_widget.hpp \
    --no-compile-commands \
    --backend latex \
    -o "$OUT/widget.tex"

# The same wording the long way round, as two processes with the IR passed
# between them on a pipe. Byte for byte the file above.
"$SPECGEN" generate --emit-ir tests/corpus/spec_widget.hpp --no-compile-commands |
    "$SPECGEN" render --from-ir - --backend latex \
        -o "$OUT/widget-piped.tex"
```

The first command writes the IR to `widget.json`. It is the same document the golden suite pins, so it is checked rather than shown.

The second command is the ordinary use of specgen. `generate` without `--emit-ir` runs the front end and a backend in a single process: the document is built, validated if requested, and rendered, and the IR is never written.

```latex
\begin{codeblock}
namespace std {
  class @\libglobal{widget}@ {
  public:
    // \ref{widget.cons}, constructors
    widget();
    explicit widget(int value);

    // \ref{widget.observers}, observers
    bool empty() const;
  };
}
\end{codeblock}

\rSec3[widget.cons]{Constructors}

\indexlibraryctor{widget}%
\begin{itemdecl}
widget();
\end{itemdecl}

\begin{itemdescr}
\pnum
\effects
Constructs a \tcode{widget} holding no value.
\end{itemdescr}

\indexlibraryctor{widget}%
\begin{itemdecl}
explicit widget(int value);
\end{itemdecl}

\begin{itemdescr}
\pnum
\effects
Constructs a \tcode{widget} holding \tcode{value}.
\end{itemdescr}

\rSec3[widget.observers]{Observers}

\indexlibrarymember{empty}{widget}%
\begin{itemdecl}
bool empty() const;
\end{itemdecl}

\begin{itemdescr}
\pnum
\effects
None.

\pnum
\returns
\tcode{true} if the widget holds no value, \tcode{false} otherwise.
\end{itemdescr}
```

The third command produces the same wording by the debugging route, as two processes with the IR passed between them on a pipe. The result is identical to the file above, byte for byte, because `render` and `generate` share the part of the driver that turns a document into wording:

```latex
\begin{codeblock}
namespace std {
  class @\libglobal{widget}@ {
  public:
    // \ref{widget.cons}, constructors
    widget();
    explicit widget(int value);

    // \ref{widget.observers}, observers
    bool empty() const;
  };
}
\end{codeblock}

\rSec3[widget.cons]{Constructors}

\indexlibraryctor{widget}%
\begin{itemdecl}
widget();
\end{itemdecl}

\begin{itemdescr}
\pnum
\effects
Constructs a \tcode{widget} holding no value.
\end{itemdescr}

\indexlibraryctor{widget}%
\begin{itemdecl}
explicit widget(int value);
\end{itemdecl}

\begin{itemdescr}
\pnum
\effects
Constructs a \tcode{widget} holding \tcode{value}.
\end{itemdescr}

\rSec3[widget.observers]{Observers}

\indexlibrarymember{empty}{widget}%
\begin{itemdecl}
bool empty() const;
\end{itemdecl}

\begin{itemdescr}
\pnum
\effects
None.

\pnum
\returns
\tcode{true} if the widget holds no value, \tcode{false} otherwise.
\end{itemdescr}
```


<a id="the-three-backends"></a>

# The Three Backends

These commands render the small hand-written `value_or` IR fixture through each backend. Because the input is identical, the outputs differ only in the syntax each backend writes.

```json
{
  "nodes": [
    {
      "type": "item",
      "decl": {
        "signatures": [
          {
            "text": "template<class U = remove_cv_t<T>> constexpr remove_cv_t<T> value_or(U&& v) const &;",
            "spans": []
          }
        ],
        "index": [
          { "kind": "member", "name": "value_or", "parent": "optional" }
        ]
      },
      "descr": {
        "elements": [
          {
            "kind": "mandates",
            "paragraphs": [
              [
                { "t": "code", "code": { "text": "is_copy_constructible_v<T>", "spans": [] } },
                { "t": "text", "text": " is " },
                { "t": "code", "code": { "text": "true", "spans": [] } },
                { "t": "text", "text": " and " },
                { "t": "code", "code": { "text": "is_convertible_v<U, T>", "spans": [] } },
                { "t": "text", "text": " is " },
                { "t": "code", "code": { "text": "true", "spans": [] } },
                { "t": "text", "text": "." }
              ]
            ]
          },
          {
            "kind": "effects",
            "paragraphs": [],
            "equivalent": {
              "text": "return has_value() ? **this : static_cast<remove_cv_t<T>>(std::forward<U>(v));",
              "spans": []
            }
          }
        ]
      }
    }
  ]
}
```

```sh
#!/bin/sh
# examples/cli/30-render-backends.sh                                  -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# One hand-written IR fixture through all three backends. The input is
# identical in each case and only the serializer changes, which is what makes
# the three outputs worth reading side by side. `render` runs no compiler, so
# the IR fixture is the whole input.
. "$(dirname -- "$0")/env.sh"

OUT=$(out_dir 30-render-backends)
cd "$REPO_ROOT"

# Draft LaTeX is the default backend; named here so the command reads the same
# as the other two.
"$SPECGEN" render --from-ir tests/golden/value_or/input.json \
    --backend latex \
    -o "$OUT/value_or.tex"

"$SPECGEN" render --from-ir tests/golden/value_or/input.json \
    --backend mpark \
    -o "$OUT/value_or.md"

# The long spelling of -o, which is the same option.
"$SPECGEN" render --from-ir tests/golden/value_or/input.json \
    --backend org \
    --output "$OUT/value_or.org"
```

Draft LaTeX:

```latex
\indexlibrarymember{value_or}{optional}%
\begin{itemdecl}
template<class U = remove_cv_t<T>> constexpr remove_cv_t<T> value_or(U&& v) const &;
\end{itemdecl}

\begin{itemdescr}
\pnum
\mandates
\tcode{is_copy_constructible_v<T>} is \tcode{true} and \tcode{is_convertible_v<U, T>} is \tcode{true}.

\pnum
\effects
Equivalent to:
\begin{codeblock}
return has_value() ? **this : static_cast<remove_cv_t<T>>(std::forward<U>(v));
\end{codeblock}
\end{itemdescr}
```

mpark/wg21 markdown:

````markdown
::: wording

```cpp
template<class U = remove_cv_t<T>> constexpr remove_cv_t<T> value_or(U&& v) const &;
```

[#]{.pnum} *Mandates*: `is_copy_constructible_v<T>` is `true` and `is_convertible_v<U, T>` is `true`.

[#]{.pnum} *Effects*: Equivalent to:

```cpp
return has_value() ? **this : static_cast<remove_cv_t<T>>(std::forward<U>(v));
```

:::
````

org, for the `wg21org` exporter:

```org
#+begin_itemdecl
template<class U = remove_cv_t<T>> constexpr remove_cv_t<T> value_or(U&& v) const &;
#+end_itemdecl

#+begin_pnum
/Mandates/: ~is_copy_constructible_v<T>~ is ~true~ and ~is_convertible_v<U, T>~ is ~true~.
#+end_pnum

#+begin_pnum
/Effects/: Equivalent to:

#+begin_codeblock
return has_value() ? **this : static_cast<remove_cv_t<T>>(std::forward<U>(v));
#+end_codeblock
#+end_pnum
```


<a id="paper-mode"></a>

# Paper Mode

Paper mode marks a complete mpark or org fragment as added wording and numbers added paragraphs as `x`, `x+1`, and so on. Mpark uses its editing-instruction div; org uses `addedblock` for rootless material and a `WG21_CHANGE` property for complete clause subtrees.

```sh
#!/bin/sh
# examples/cli/40-paper-mode.sh                                       -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Paper mode marks a complete mpark or org fragment as added wording and
# numbers its paragraphs x, x+1, and so on.
. "$(dirname -- "$0")/env.sh"

OUT=$(out_dir 40-paper-mode)
cd "$REPO_ROOT"

"$SPECGEN" render --from-ir tests/golden/paper_mode/input.json \
    --backend mpark \
    --paper \
    -o "$OUT/paper-mode.md"

"$SPECGEN" render --from-ir tests/golden/paper_mode/input.json \
    --backend org \
    --paper \
    -o "$OUT/paper-mode.org"
```

````markdown
::: add

```cpp
class gadget {
public:
  constexpr bool ready() const noexcept;
};
```

::: wording

## Observers [gadget.observe]{- .sref} {-}

```cpp
constexpr bool ready() const noexcept;
```

[x]{.pnum} *Constraints*:

- [x.#]{.pnum} `is_copy_constructible_v<T>` is `true`,
- [x.#]{.pnum} `is_move_constructible_v<T>` is `true`.

[x+1]{.pnum} *Returns*: `true` if and only if the gadget is ready.

[x+2]{.pnum} A second paragraph, so the added numbering has to advance.

[x+3]{.pnum} A free paragraph closing the subclause.

:::

::: wording

## Modifiers [gadget.mod]{- .sref} {-}

```cpp
constexpr void reset() noexcept;
```

[x+4]{.pnum} *Effects*: Resets the gadget.

:::

:::
````

The same input in org:

```org
#+begin_addedblock
#+begin_codeblock
class gadget {
public:
  constexpr bool ready() const noexcept;
};
#+end_codeblock
#+end_addedblock

** Observers [gadget.observe]
:PROPERTIES:
:CUSTOM_ID: gadget.observe
:UNNUMBERED: t
:WG21_WORDING: t
:WG21_CHANGE: add
:END:

#+begin_itemdecl
constexpr bool ready() const noexcept;
#+end_itemdecl

#+begin_pnum x
/Constraints/:

- ~is_copy_constructible_v<T>~ is ~true~,
- ~is_move_constructible_v<T>~ is ~true~.
#+end_pnum

#+begin_pnum x+1
/Returns/: ~true~ if and only if the gadget is ready.
#+end_pnum

#+begin_pnum x+2
A second paragraph, so the added numbering has to advance.
#+end_pnum

#+begin_pnum x+3
A free paragraph closing the subclause.
#+end_pnum

** Modifiers [gadget.mod]
:PROPERTIES:
:CUSTOM_ID: gadget.mod
:UNNUMBERED: t
:WG21_WORDING: t
:WG21_CHANGE: add
:END:

#+begin_itemdecl
constexpr void reset() noexcept;
#+end_itemdecl

#+begin_pnum x+4
/Effects/: Resets the gadget.
#+end_pnum
```


<a id="the-wording-diff"></a>

# The Wording Diff

A baseline turns the render into a diff: the wording comes out marked with what the revision added and removed, in each backend's own editing form. The corpus pair is one header a revision apart; `--baseline-header` parses the old one, `--baseline-ir` would read a saved IR instead. Only the sections that changed are kept by default; `--diff-context items` also elides the members inside them that did not.

```sh
#!/bin/sh
# examples/cli/45-wording-diff.sh                                     -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The wording diff: a header rendered against a baseline, the wording coming
# out marked with what the revision added and removed. The corpus pair is the
# same header one revision apart; `--baseline-header` parses the old one.
. "$(dirname -- "$0")/env.sh"

OUT=$(out_dir 45-wording-diff)
cd "$REPO_ROOT"

"$SPECGEN" generate tests/corpus/diff/gadget.after.hpp \
    --baseline-header tests/corpus/diff/gadget.before.hpp \
    --backend mpark \
    --no-compile-commands \
    -o "$OUT/gadget-diff.md"

"$SPECGEN" generate tests/corpus/diff/gadget.after.hpp \
    --baseline-header tests/corpus/diff/gadget.before.hpp \
    --backend org \
    --no-compile-commands \
    -o "$OUT/gadget-diff.org"

# The `items` context elides the members that did not change.
"$SPECGEN" generate tests/corpus/diff/gadget.after.hpp \
    --baseline-header tests/corpus/diff/gadget.before.hpp \
    --diff-context items \
    --backend mpark \
    --no-compile-commands \
    -o "$OUT/gadget-diff-items.md"
```

````markdown
```cpp
namespace std {
  class gadget {
  public:
    // @[gadget.observe]{- .sref}@, observers
    bool ready() const @[noexcept]{.add}@;
    int level() const;

    // @[gadget.mod]{- .sref}@, modifiers
    void @[reset]{.rm}@@[clear]{.add}@();
    void arm(int level);

    @[// [gadget.query]{- .sref}, queries]{.add}@
    @[bool armed() const;]{.add}@
  };
}
```

::: wording

## Observers [gadget.observe]{- .sref} {-}

```cpp
bool ready() const @[noexcept]{.add}@;
```

[#]{.pnum} *Returns*: `true` if the gadget is [ready.]{.rm}[armed.]{.add}

::: add

[x]{.pnum} *Complexity*: Constant.

:::

```cpp
int level() const;
```

[#]{.pnum} *Returns*: The current level.

[#]{.pnum} *Remarks*: A gadget starts at level zero.

:::

::: wording

## Modifiers [gadget.mod]{- .sref} {-}

::: rm

```cpp
void reset();
```

[#]{.pnum} *Effects*: Resets the gadget to level zero.

:::

::: add

```cpp
void clear();
```

[x+1]{.pnum} *Effects*: Clears the gadget, returning it to level zero.

:::

```cpp
void arm(int level);
```

[#]{.pnum} *Preconditions*: `level` is [positive.]{.rm}[greater than zero.]{.add}

[#]{.pnum} *Effects*: Arms the gadget at `level`.

::: add

[x+2]{.pnum} Arming an armed gadget replaces its level.

:::

:::

::: add

::: wording

## Queries [gadget.query]{- .sref} {-}

```cpp
bool armed() const;
```

[x+3]{.pnum} *Returns*: `true` if the gadget has been armed.

:::

:::
````

The same diff in org:

```org
#+begin_codeblock
namespace std {
  class gadget {
  public:
    // \ref{gadget.observe}, observers
    bool ready() const @\added{noexcept}@;
    int level() const;

    // \ref{gadget.mod}, modifiers
    void @\removed{reset}@@\added{clear}@();
    void arm(int level);

    @\added{// \ref{gadget.query}, queries}@
    @\added{bool armed() const;}@
  };
}
#+end_codeblock

** Observers [gadget.observe]
:PROPERTIES:
:CUSTOM_ID: gadget.observe
:UNNUMBERED: t
:WG21_WORDING: t
:END:

#+begin_itemdecl
bool ready() const @\added{noexcept}@;
#+end_itemdecl

#+begin_pnum
/Returns/: ~true~ if the gadget is [[delete:][ready.]][[insert:][armed.]]
#+end_pnum

#+begin_addedblock
#+begin_pnum x
/Complexity/: Constant.
#+end_pnum
#+end_addedblock

#+begin_itemdecl
int level() const;
#+end_itemdecl

#+begin_pnum
/Returns/: The current level.
#+end_pnum

#+begin_pnum
/Remarks/: A gadget starts at level zero.
#+end_pnum

** Modifiers [gadget.mod]
:PROPERTIES:
:CUSTOM_ID: gadget.mod
:UNNUMBERED: t
:WG21_WORDING: t
:END:

#+begin_removedblock
#+begin_itemdecl
void reset();
#+end_itemdecl

#+begin_pnum
/Effects/: Resets the gadget to level zero.
#+end_pnum
#+end_removedblock

#+begin_addedblock
#+begin_itemdecl
void clear();
#+end_itemdecl

#+begin_pnum x+1
/Effects/: Clears the gadget, returning it to level zero.
#+end_pnum
#+end_addedblock

#+begin_itemdecl
void arm(int level);
#+end_itemdecl

#+begin_pnum
/Preconditions/: ~level~ is [[delete:][positive.]][[insert:][greater than zero.]]
#+end_pnum

#+begin_pnum
/Effects/: Arms the gadget at ~level~.
#+end_pnum

#+begin_addedblock
#+begin_pnum x+2
Arming an armed gadget replaces its level.
#+end_pnum
#+end_addedblock

** Queries [gadget.query]
:PROPERTIES:
:CUSTOM_ID: gadget.query
:UNNUMBERED: t
:WG21_WORDING: t
:WG21_CHANGE: add
:END:

#+begin_itemdecl
bool armed() const;
#+end_itemdecl

#+begin_pnum x+3
/Returns/: ~true~ if the gadget has been armed.
#+end_pnum
```

And under the `items` context, where `level()` collapses to the ellipsis:

````markdown
```cpp
namespace std {
  class gadget {
  public:
    // @[gadget.observe]{- .sref}@, observers
    bool ready() const @[noexcept]{.add}@;
    int level() const;

    // @[gadget.mod]{- .sref}@, modifiers
    void @[reset]{.rm}@@[clear]{.add}@();
    void arm(int level);

    @[// [gadget.query]{- .sref}, queries]{.add}@
    @[bool armed() const;]{.add}@
  };
}
```

::: wording

## Observers [gadget.observe]{- .sref} {-}

```cpp
bool ready() const @[noexcept]{.add}@;
```

[#]{.pnum} *Returns*: `true` if the gadget is [ready.]{.rm}[armed.]{.add}

::: add

[x]{.pnum} *Complexity*: Constant.

:::

[…]

:::

::: wording

## Modifiers [gadget.mod]{- .sref} {-}

::: rm

```cpp
void reset();
```

[#]{.pnum} *Effects*: Resets the gadget to level zero.

:::

::: add

```cpp
void clear();
```

[x+1]{.pnum} *Effects*: Clears the gadget, returning it to level zero.

:::

```cpp
void arm(int level);
```

[#]{.pnum} *Preconditions*: `level` is [positive.]{.rm}[greater than zero.]{.add}

[#]{.pnum} *Effects*: Arms the gadget at `level`.

::: add

[x+2]{.pnum} Arming an armed gadget replaces its level.

:::

:::

::: add

::: wording

## Queries [gadget.query]{- .sref} {-}

[…]

:::

:::
````


<a id="the-wording-diff-draft"></a>

## Against the working draft

The baseline can also be the standard's own wording. `--baseline-draft` takes the draft's `source` directory and reads, by stable name, the sections the header generates, so a paper changes what it changes and the rest comes from the draft. The header here declares one item of [optional.assign]; its description is reworded, and the draft's other items, which the header leaves out, come out removed.

```sh
#!/bin/sh
# examples/cli/47-wording-diff-draft.sh                               -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The wording diff against the C++ working draft itself: no earlier revision of
# the header, just the draft's LaTeX. `--baseline-draft` names the draft's
# source directory and reads, by stable name, the sections the header
# generates. Here the "draft" is the importer's corpus slice of
# [optional.assign], staged as the `.tex` file the flag looks for.
. "$(dirname -- "$0")/env.sh"

OUT=$(out_dir 47-wording-diff-draft)
cd "$REPO_ROOT"

DRAFT=$(mktemp -d)
trap 'rm -rf "$DRAFT"' EXIT
cp tests/corpus/standard-slices/optional.assign.slice "$DRAFT/utilities.tex"

"$SPECGEN" generate tests/corpus/diff/optional_assign.hpp \
    --baseline-draft "$DRAFT" \
    --backend mpark \
    --no-compile-commands \
    -o "$OUT/optional-assign-diff.md"
```

````markdown
::: add

```cpp
namespace std {
  struct nullopt_t {};
}
```

```cpp
namespace std {
  template<class T>
  class optional {
  public:
    // @[optional.assign]{- .sref}@, assignment
    constexpr optional& operator=(nullopt_t) noexcept;
  };
}
```

:::

::: wording

## Assignment [optional.assign]{- .sref} {-}

```cpp
constexpr optional@[<T>]{.rm}@& operator=(nullopt_t) noexcept;
```

[#]{.pnum} *Effects*: [If `*this` contains a value, calls `$val$.T::~T()` to destroy the contained value; otherwise no effect.]{.rm}[Destroys the contained value, if there is one.]{.add}

[#]{.pnum} *Postconditions*: `*this` does not contain a value.

[#]{.pnum} *Returns*: `*this`.

::: rm

```cpp
constexpr optional<T>& operator=(const optional& rhs);
```

[#]{.pnum} *Effects*: See [Table \[optional.assign.copy\]](#optional.assign.copy).

| | `*this` contains a value | `*this` does not contain a value |
|---|---|---|
| **`rhs` contains a value** | assigns `rhs.$val$` to `$val$` | direct-non-list-initializes `$val$` with `rhs.$val$` |
| **`rhs` does not contain a value** | destroys the contained value by calling `$val$.T::~T()` | no effect |
: [`optional::operator=(const optional&)` effects]{#optional.assign.copy}

[#]{.pnum} *Postconditions*: `rhs.has_value() == this->has_value()`.

[#]{.pnum} *Returns*: `*this`.

[#]{.pnum} *Remarks*: If any exception is thrown, the result of the expression `this->has_value()` remains unchanged. If an exception is thrown during the call to `T`'s copy constructor, no effect. If an exception is thrown during the call to `T`'s copy assignment, the state of its contained value is as defined by the exception safety guarantee of `T`'s copy assignment. This operator is defined as deleted unless `is_copy_constructible_v<T>` is `true` and `is_copy_assignable_v<T>` is `true`. If `is_trivially_copy_constructible_v<T> &&` `is_trivially_copy_assignable_v<T> &&` `is_trivially_destructible_v<T>` is `true`, this assignment operator is trivial.

:::

::: rm

```cpp
constexpr optional& operator=(optional&& rhs) noexcept($see below$);
```

[#]{.pnum} *Constraints*: `is_move_constructible_v<T>` is `true` and `is_move_assignable_v<T>` is `true`.

[#]{.pnum} *Effects*: See [Table \[optional.assign.move\]](#optional.assign.move). The result of the expression `rhs.has_value()` remains unchanged.

| | `*this` contains a value | `*this` does not contain a value |
|---|---|---|
| **`rhs` contains a value** | assigns `std::move(rhs.$val$)` to `$val$` | direct-non-list-initializes `$val$` with `std::move(rhs.$val$)` |
| **`rhs` does not contain a value** | destroys the contained value by calling `$val$.T::~T()` | no effect |
: [`optional::operator=(optional&&)` effects]{#optional.assign.move}

[#]{.pnum} *Postconditions*: `rhs.has_value() == this->has_value()`.

[#]{.pnum} *Returns*: `*this`.

[#]{.pnum} *Remarks*: The exception specification is equivalent to:

```cpp
is_nothrow_move_assignable_v<T> && is_nothrow_move_constructible_v<T>
```

[#]{.pnum} If any exception is thrown, the result of the expression `this->has_value()` remains unchanged. If an exception is thrown during the call to `T`'s move constructor, the state of `rhs.$val$` is determined by the exception safety guarantee of `T`'s move constructor. If an exception is thrown during the call to `T`'s move assignment, the states of `$val$` and `rhs.$val$` are determined by the exception safety guarantee of `T`'s move assignment. If `is_trivially_move_constructible_v<T> &&` `is_trivially_move_assignable_v<T> &&` `is_trivially_destructible_v<T>` is `true`, this assignment operator is trivial.

:::

::: rm

```cpp
template<class U = remove_cv_t<T>> constexpr optional& operator=(U&& v);
```

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `is_same_v<remove_cvref_t<U>, optional>` is `false`,
- [#.#]{.pnum} `conjunction_v<is_scalar<T>, is_same<T, decay_t<U>>>` is `false`,
- [#.#]{.pnum} `is_constructible_v<T, U>` is `true`, and
- [#.#]{.pnum} `is_assignable_v<T&, U>` is `true`.

[#]{.pnum} *Effects*: If `*this` contains a value, assigns `std::forward<U>(v)` to `$val$`; otherwise direct-non-list-initializes `$val$` with `std::forward<U>(v)`.

[#]{.pnum} *Postconditions*: `*this` contains a value.

[#]{.pnum} *Returns*: `*this`.

[#]{.pnum} *Remarks*: If any exception is thrown, the result of the expression `this->has_value()` remains unchanged. If an exception is thrown during the call to `T`'s constructor, the state of `v` is determined by the exception safety guarantee of `T`'s constructor. If an exception is thrown during the call to `T`'s assignment, the states of `$val$` and `v` are determined by the exception safety guarantee of `T`'s assignment.

:::

::: rm

```cpp
template<class U> constexpr optional<T>& operator=(const optional<U>& rhs);
```

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `is_constructible_v<T, const U&>` is `true`,
- [#.#]{.pnum} `is_assignable_v<T&, const U&>` is `true`,
- [#.#]{.pnum} `$converts-from-any-cvref$<T, optional<U>>` is `false`,
- [#.#]{.pnum} `is_assignable_v<T&, optional<U>&>` is `false`,
- [#.#]{.pnum} `is_assignable_v<T&, optional<U>&&>` is `false`,
- [#.#]{.pnum} `is_assignable_v<T&, const optional<U>&>` is `false`, and
- [#.#]{.pnum} `is_assignable_v<T&, const optional<U>&&>` is `false`.

[#]{.pnum} *Effects*: See [Table \[optional.assign.copy.templ\]](#optional.assign.copy.templ).

| | `*this` contains a value | `*this` does not contain a value |
|---|---|---|
| **`rhs` contains a value** | assigns `rhs.operator*()` to `$val$` | direct-non-list-initializes `$val$` with `rhs.operator*()` |
| **`rhs` does not contain a value** | destroys the contained value by calling `$val$.T::~T()` | no effect |
: [`optional::operator=(const optional<U>&)` effects]{#optional.assign.copy.templ}

[#]{.pnum} *Postconditions*: `rhs.has_value() == this->has_value()`.

[#]{.pnum} *Returns*: `*this`.

[#]{.pnum} *Remarks*: If any exception is thrown, the result of the expression `this->has_value()` remains unchanged. If an exception is thrown during the call to `T`'s constructor, the state of `rhs.$val$` is determined by the exception safety guarantee of `T`'s constructor. If an exception is thrown during the call to `T`'s assignment, the states of `$val$` and `rhs.$val$` are determined by the exception safety guarantee of `T`'s assignment.

:::

::: rm

```cpp
template<class U> constexpr optional<T>& operator=(optional<U>&& rhs);
```

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `is_constructible_v<T, U>` is `true`,
- [#.#]{.pnum} `is_assignable_v<T&, U>` is `true`,
- [#.#]{.pnum} `$converts-from-any-cvref$<T, optional<U>>` is `false`,
- [#.#]{.pnum} `is_assignable_v<T&, optional<U>&>` is `false`,
- [#.#]{.pnum} `is_assignable_v<T&, optional<U>&&>` is `false`,
- [#.#]{.pnum} `is_assignable_v<T&, const optional<U>&>` is `false`, and
- [#.#]{.pnum} `is_assignable_v<T&, const optional<U>&&>` is `false`.

[#]{.pnum} *Effects*: See [Table \[optional.assign.move.templ\]](#optional.assign.move.templ). The result of the expression `rhs.has_value()` remains unchanged.

| | `*this` contains a value | `*this` does not contain a value |
|---|---|---|
| **`rhs` contains a value** | assigns `std::move(rhs).operator*()` to `$val$` | direct-non-list-initializes `$val$` with `std::move(rhs).operator*()` |
| **`rhs` does not contain a value** | destroys the contained value by calling `$val$.T::~T()` | no effect |
: [`optional::operator=(optional<U>&&)` effects]{#optional.assign.move.templ}

[#]{.pnum} *Postconditions*: `rhs.has_value() == this->has_value()`.

[#]{.pnum} *Returns*: `*this`.

[#]{.pnum} *Remarks*: If any exception is thrown, the result of the expression `this->has_value()` remains unchanged. If an exception is thrown during the call to `T`'s constructor, the state of `rhs.$val$` is determined by the exception safety guarantee of `T`'s constructor. If an exception is thrown during the call to `T`'s assignment, the states of `$val$` and `rhs.$val$` are determined by the exception safety guarantee of `T`'s assignment.

:::

::: rm

```cpp
template<class... Args> constexpr T& emplace(Args&&... args);
```

[#]{.pnum} *Constraints*: `is_constructible_v<T, Args...>` is `true`.

[#]{.pnum} *Effects*: Calls `*this = nullopt`. Then direct-non-list-initializes `$val$` with `std::forward<Args>(args)...`.

[#]{.pnum} *Postconditions*: `*this` contains a value.

[#]{.pnum} *Returns*: `$val$`.

[#]{.pnum} *Throws*: Any exception thrown by the selected constructor of `T`.

[#]{.pnum} *Remarks*: If an exception is thrown during the call to `T`'s constructor, `*this` does not contain a value, and the previous `$val$` (if any) has been destroyed.

:::

::: rm

```cpp
template<class U, class... Args> constexpr T& emplace(initializer_list<U> il, Args&&... args);
```

[#]{.pnum} *Constraints*: `is_constructible_v<T, initializer_list<U>&, Args...>` is `true`.

[#]{.pnum} *Effects*: Calls `*this = nullopt`. Then direct-non-list-initializes `$val$` with `il, std::forward<Args>(args)...`.

[#]{.pnum} *Postconditions*: `*this` contains a value.

[#]{.pnum} *Returns*: `$val$`.

[#]{.pnum} *Throws*: Any exception thrown by the selected constructor of `T`.

[#]{.pnum} *Remarks*: If an exception is thrown during the call to `T`'s constructor, `*this` does not contain a value, and the previous `$val$` (if any) has been destroyed.

:::

:::
````


<a id="fragment-output"></a>

# Fragment Output

`--split` writes one file per top-level stable-name section and prints a manifest in document order. The same input goes through all three backends; only the extensions and the rendered syntax differ. Consumers should rely on the manifest, which lists the fragments in document order.

```sh
#!/bin/sh
# examples/cli/50-fragments.sh                                        -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# `--split` writes one file per top-level stable-name section and prints a
# manifest in document order. Each backend is run from inside its own output
# directory with a *relative* `--split wording`, so the manifest holds
# `wording/optional.ctor.tex` rather than an absolute path that would differ on
# every machine -- the same thing the split-mode golden does, and what lets
# these files be compared against it.
. "$(dirname -- "$0")/env.sh"

OUT=$(out_dir 50-fragments)
IR=$REPO_ROOT/tests/golden/optional/expected.json

for backend in latex mpark; do
    mkdir -p "$OUT/$backend"
    (
        cd "$OUT/$backend"
        "$SPECGEN" render --from-ir "$IR" \
            --backend "$backend" \
            --split wording > manifest
    )
done

# The org split names its root fragment explicitly; without `--root` the name
# is derived from the longest shared prefix of the section names.
mkdir -p "$OUT/org"
(
    cd "$OUT/org"
    "$SPECGEN" render --from-ir "$IR" \
        --backend org \
        --root optional.syn \
        --split wording > manifest
)
```

The org run names its root fragment explicitly, which is why its manifest begins with `optional.syn.org`, whereas the other two derive `optional` from the sections' common prefix:

```text
wording/optional.syn.org
wording/optional.ctor.org
wording/optional.assign.org
wording/optional.observe.org
wording/optional.monadic.org
wording/optional.mod.org
```

One fragment from each backend, showing the same section three ways:

```latex
\rSec3[optional.ctor]{Constructors}

\indexlibraryctor{optional}%
\begin{itemdecl}
constexpr optional() noexcept;
constexpr optional(nullopt_t) noexcept;
\end{itemdecl}

\begin{itemdescr}
\pnum
\ensures
\tcode{*this} does not contain a value.
\end{itemdescr}

\indexlibraryctor{optional}%
\begin{itemdecl}
constexpr optional(const optional& rhs);
\end{itemdecl}

\begin{itemdescr}
\pnum
\constraints
\tcode{is_copy_constructible_v<T>} is \tcode{true}.

\pnum
\effects
If \tcode{rhs} contains a value, direct-non-list-initializes the contained value with \tcode{*rhs}.

\pnum
\ensures
\tcode{has_value()} is equal to \tcode{rhs.has_value()}.
\end{itemdescr}

\indexlibraryctor{optional}%
\begin{itemdecl}
template<class... Args>
  constexpr explicit optional(in_place_t, Args&&... args);
\end{itemdecl}

\begin{itemdescr}
\pnum
\constraints
\tcode{is_constructible_v<T, Args...>} is \tcode{true}.

\pnum
\effects
Direct-non-list-initializes the contained value with \tcode{std::forward<Args>(args)...}.

\pnum
\ensures
\tcode{has_value()} is \tcode{true}.
\end{itemdescr}

\indexlibraryctor{optional}%
\begin{itemdecl}
template<class U>
  constexpr explicit(!is_convertible_v<U, T>) optional(const optional<U>& rhs);
\end{itemdecl}

\begin{itemdescr}
\pnum
\constraints
\begin{itemize}
\item \tcode{is_constructible_v<T, const U\&>} is \tcode{true},
\item \tcode{is_convertible_v<U, T>} is \tcode{true},
\item \tcode{is_same_v<T, U>} is \tcode{false},
\item \tcode{is_constructible_v<T, optional<U>>} is \tcode{false}.
\end{itemize}

\pnum
\effects
If \tcode{rhs} contains a value, direct-non-list-initializes the contained value with \tcode{*rhs}.

\pnum
\ensures
\tcode{has_value()} is equal to \tcode{rhs.has_value()}.
\end{itemdescr}
```

````markdown
::: wording

## Constructors [optional.ctor]{- .sref} {-}

```cpp
constexpr optional() noexcept;
constexpr optional(nullopt_t) noexcept;
```

[#]{.pnum} *Postconditions*: `*this` does not contain a value.

```cpp
constexpr optional(const optional& rhs);
```

[#]{.pnum} *Constraints*: `is_copy_constructible_v<T>` is `true`.

[#]{.pnum} *Effects*: If `rhs` contains a value, direct-non-list-initializes the contained value with `*rhs`.

[#]{.pnum} *Postconditions*: `has_value()` is equal to `rhs.has_value()`.

```cpp
template<class... Args>
  constexpr explicit optional(in_place_t, Args&&... args);
```

[#]{.pnum} *Constraints*: `is_constructible_v<T, Args...>` is `true`.

[#]{.pnum} *Effects*: Direct-non-list-initializes the contained value with `std::forward<Args>(args)...`.

[#]{.pnum} *Postconditions*: `has_value()` is `true`.

```cpp
template<class U>
  constexpr explicit(!is_convertible_v<U, T>) optional(const optional<U>& rhs);
```

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `is_constructible_v<T, const U&>` is `true`,
- [#.#]{.pnum} `is_convertible_v<U, T>` is `true`,
- [#.#]{.pnum} `is_same_v<T, U>` is `false`,
- [#.#]{.pnum} `is_constructible_v<T, optional<U>>` is `false`.

[#]{.pnum} *Effects*: If `rhs` contains a value, direct-non-list-initializes the contained value with `*rhs`.

[#]{.pnum} *Postconditions*: `has_value()` is equal to `rhs.has_value()`.

:::
````

```org
#+begin_codeblock
namespace std {
  struct nullopt_t {};
}
#+end_codeblock

#+begin_codeblock
namespace std {
  struct in_place_t {
    explicit in_place_t() = default;
  };
}
#+end_codeblock

#+begin_codeblock
namespace std {
  template<class T>
  class optional {
  public:
    using value_type = T;

    // \ref{optional.ctor}, constructors
    constexpr optional() noexcept;
    constexpr optional(nullopt_t) noexcept;

    constexpr optional(const optional& rhs);

    template<class... Args> constexpr explicit optional(in_place_t, Args&&... args);

    template<class U>
    constexpr explicit(!is_convertible_v<U, T>) optional(const optional<U>& rhs);

    // \ref{optional.assign}, assignment
    template<class... Args> constexpr T& emplace(Args&&... args);

    // \ref{optional.observe}, observers
    constexpr bool has_value() const noexcept;
    constexpr const T& operator*() const&;

    template<class U = remove_cv_t<T>> constexpr remove_cv_t<T> value_or(U&& u) const&;

    // \ref{optional.monadic}, monadic operations
    template<class F> constexpr @\seebelow@ transform(F&& f) const&;

    // \ref{optional.mod}, modifiers
    constexpr void reset() noexcept;

    friend constexpr bool operator==(const optional& x, const optional& y);

  private:
    T @\exposid{value}@;              // exposition only
    bool @\exposid{engaged}@ = false; // exposition only
  };
}
#+end_codeblock
```


<a id="a-paper-of-several-headers"></a>

# A Paper Of Several Headers

A paper proposing several headers is several specgen documents and one invocation. `generate` takes one header per document and validates across the union of their documented names, so a name specified by a sibling document is not foreign. It writes the assembled clause, the per-clause fragments, or both.

```sh
#!/bin/sh
# examples/cli/55-paper.sh                                            -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# A paper of several headers in one invocation. `generate` takes one header per
# document and renders them as one paper -- validated across the union of their
# documented names, so a name specified by a sibling document is not foreign --
# writing the assembled clause with `-o`, the per-clause fragments with
# `--split`, and a Makefile dependency fragment with `--depfile`.
#
# The headers are copied into the output directory and everything runs from
# there with relative paths, for the reason 50-fragments.sh runs from inside its
# own: a manifest or a dependency fragment holding an absolute path would differ
# on every machine. It also makes the capture read like the project it is an
# example for -- headers under include/, wording written beside them.
. "$(dirname -- "$0")/env.sh"


OUT=$(out_dir 55-paper)
CORPUS=$REPO_ROOT/tests/corpus

mkdir -p "$OUT/include/support"
cp "$CORPUS/spec_paper_main.hpp" "$CORPUS/spec_paper_sibling.hpp" "$OUT/include/"
cp "$CORPUS/support/spec_paper_traits.hpp" "$OUT/include/support/"

cd "$OUT"

# One parse of both headers, three outputs: the whole, the pieces, and the
# dependency fragment that says what the first two were built from.
"$SPECGEN" generate include/spec_paper_main.hpp include/spec_paper_sibling.hpp \
    --validate \
    --no-compile-commands \
    -o expected.tex \
    --split wording \
    --root paper.mix.syn --root paper.blend.syn \
    --depfile wording.d > manifest
```

`--split` names the pieces, in document then section order:

```text
wording/paper.mix.syn.tex
wording/paper.mix.tex
wording/paper.blend.syn.tex
wording/paper.blend.tex
```

`-o` names the whole, which consists of the same pieces in the same order without the fragment boundaries. `golden.paper_whole.is_fragments` verifies this, because a paper that assembles its clause from fragments and one that renders it whole must agree.

`--depfile` writes what make needs to know to rebuild any of it. The prerequisites are every file the parses read. They include `include/support/spec_paper_traits.hpp`, which neither header names on the command line and no rendered wording mentions; it is nevertheless the dependency that matters most, because a docblock in it changes the generated clause, and a paper that did not depend on it would become stale without notice. The empty rule per prerequisite corresponds to `-MP`: when a header is deleted or renamed, the next build regenerates the wording rather than failing on a prerequisite that no longer exists.

```makefile
expected.tex wording/paper.mix.syn.tex wording/paper.mix.tex wording/paper.blend.syn.tex wording/paper.blend.tex: \
  include/spec_paper_main.hpp \
  include/spec_paper_sibling.hpp \
  include/support/spec_paper_traits.hpp

include/spec_paper_main.hpp:
include/spec_paper_sibling.hpp:
include/support/spec_paper_traits.hpp:
```


<a id="validation"></a>

# Validation

`--validate` runs the validators before rendering. These commands render IR fixtures; `generate --validate` behaves identically.

```sh
#!/bin/sh
# examples/cli/60-validate.sh                                         -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Validators run in the normal render path. Clang-free, like every other
# render-mode script.
. "$(dirname -- "$0")/env.sh"

OUT=$(out_dir 60-validate)
cd "$REPO_ROOT"

# A document that validates clean still renders.
"$SPECGEN" render --from-ir tests/golden/value_or/input.json \
    --validate \
    --backend latex \
    -o "$OUT/value_or-validated.tex"

# A finding at Error severity is reported on stderr, rendering is skipped, and
# the exit status is 1. That non-zero exit is this example's expected outcome
# rather than a failure, so it is caught here instead of tripping `set -e`.
if "$SPECGEN" render --from-ir tests/golden/validate_coverage/input.json \
    --validate \
    --backend latex \
    -o "$OUT/coverage.tex" 2> "$OUT/coverage.diag"; then
    printf 'expected a validation failure from validate_coverage, got success\n' >&2
    exit 1
fi
```

A clean document renders as usual and prints nothing further. A finding at error severity is reported on standard error, the render is skipped, and the exit status is 1, which is why the script must catch the failure rather than let it abort:

```text
specgen: widget/synopsis: error: `resize` is declared in the synopsis but is not described, `\omit`ted, `\merge`d, or defaulted
specgen: widget/synopsis: error: the description of `reserve` is routed to [widget.nowhere], which is not a section in this document
specgen: widget/synopsis: error: the description of `shrink` is routed to no section: it is written in the class body under no `\ref` group and carries no `\at`
```

`generate` reports findings about the docblock markup itself while still producing output for the header it parsed:

```sh
#!/bin/sh
# examples/cli/65-diagnostics.sh                                      -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# `generate` reports source and docblock findings while still emitting IR for
# the header it parsed. Run from the header's own directory, and naming the
# header by file name alone, because the findings carry the path specgen was
# given -- an absolute path here would make the captured text machine-specific.
# That is exactly how the diagnose-mode golden runs, which is what lets this
# be compared against golden.diagnostics' expected.diag.
. "$(dirname -- "$0")/env.sh"

OUT=$(out_dir 65-diagnostics)

(
    cd "$REPO_ROOT/tests/corpus"
    # It exits 1, after writing the IR: some of the findings are errors.
    "$SPECGEN" generate --emit-ir spec_diagnostics.hpp \
        --no-compile-commands \
        -o "$OUT/diagnostics.json" 2> "$OUT/diagnostics.diag" || test $? -eq 1
)
```

```text
spec_diagnostics.hpp:63: error: unknown tag \remark; line kept as prose
spec_diagnostics.hpp:73: warning: duplicate \effects element; both kept
spec_diagnostics.hpp:91: note: \effects appears after \remarks; output is canonicalized
spec_diagnostics.hpp:95: error: unknown tag \effect; line kept as prose
spec_diagnostics.hpp:98: warning: END [gadget.wrong] does not match open section [gadget.obs]
spec_diagnostics.hpp:100: warning: END [gadget.extra] has no open section to close
spec_diagnostics.hpp:102: warning: malformed \rSec marker: digits: value out of range (comment offset 8)
spec_diagnostics.hpp:104: warning: malformed \rSec marker: expected '{' (comment offset 26)
spec_diagnostics.hpp:108: warning: unrecognized section header [gadget.mod]; use \rSec<depth>[gadget.mod]{title}
specgen: 2 findings in the documented header fail the run; the output is written
```


<a id="explicit-include-path"></a>

# Explicit Include Path

A header that includes another requires an include directory, which may be supplied in two ways: after a bare `--`, or in a compilation database.

```sh
#!/bin/sh
# examples/cli/70-include-path.sh                                     -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The two ways compiler arguments reach the front end, over one header that
# needs an include directory. Both must produce the same IR -- that is the
# point of running them side by side, and both captures are compared against
# the *same* checked-in golden.
#
# The compilation database is written to a temporary directory and thrown away
# rather than captured: its JSON bakes absolute paths, so it would differ on
# every machine. Only the IR it produces is kept, which carries no paths at all.
. "$(dirname -- "$0")/env.sh"

OUT=$(out_dir 70-include-path)
cd "$REPO_ROOT"

# Arguments after the first bare `--` go straight to Clang.
"$SPECGEN" generate --emit-ir \
    tests/corpus/include_path/consumer/spec_include.hpp \
    --no-compile-commands \
    -o "$OUT/include-path.json" \
    -- -I tests/corpus/include_path

DB=$(mktemp -d)
trap 'rm -rf -- "$DB"' EXIT

HEADER=$REPO_ROOT/tests/corpus/include_path/consumer/spec_include.hpp
printf '[{"directory":"%s","file":"%s","arguments":["c++","-I","%s","-c","-o","spec_include.o","%s"]}]\n' \
    "$DB" "$HEADER" "$REPO_ROOT/tests/corpus/include_path" "$HEADER" \
    > "$DB/compile_commands.json"

"$SPECGEN" generate --emit-ir \
    tests/corpus/include_path/consumer/spec_include.hpp \
    --compile-commands "$DB" \
    -o "$OUT/include-path-from-db.json"
```

Both produce the same IR, byte for byte, and both are compared against the same golden file, so any difference between them is a genuine one.


<a id="declaration-interleave"></a>

# Declaration Interleave

`dump-decls` prints the sequence of declarations and comments from which the front end builds the document. It is the first tool to consult when a section is unexpectedly empty.

```sh
#!/bin/sh
# examples/cli/80-dump-decls.sh                                       -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# `dump-decls` prints the declaration/comment event stream the front end builds
# the document from. Useful when a section is empty and should not be.
. "$(dirname -- "$0")/env.sh"

OUT=$(out_dir 80-dump-decls)
cd "$REPO_ROOT"

"$SPECGEN" dump-decls tests/corpus/spec_widget.hpp \
    --no-compile-commands > "$OUT/widget-decls.txt"
```

```text
[0] COMMENT // tests/corpus/spec_widget.hpp                                    -*-C++-*-
[137] COMMENT // Hand-curated corpus header (decision hermetic-corpus). A Beman-style shape
[960] DECL CXXRecord widget
[989] COMMENT // \ref{widget.cons}, constructors
[1075] COMMENT // \ref{widget.observers}, observers
[1172] COMMENT // \rSec3[widget.cons]{Constructors}
[1210] COMMENT //! \effects Constructs a `widget` holding no value.
[1263] DECL CXXConstructor widget
[1296] COMMENT //! \effects Constructs a `widget` holding `value`.
[1348] DECL CXXConstructor widget
[1394] COMMENT // \rSec3[widget.observers]{Observers}
[1434] COMMENT //! \effects None.
[1522] DECL CXXMethod empty
[1576] COMMENT // namespace demo
[1602] COMMENT // BEMAN_SPECGEN_CORPUS_SPEC_WIDGET_HPP
```


<a id="converting-a-header"></a>

# Converting a Header

`split_inline`, installed beside `specgen`, turns a header with in-class member definitions into the shape specgen reads: the class body declaration-only, each definition after the class inside the namespace block that already encloses it, hidden friends and defaulted or deleted members untouched. Without options every comment stays with its declaration; `--move-comments` carries the `//!` docblocks to the definitions and places each under the `\rSec` section its `\ref` group names, so `generate` renders the same wording from the converted header as from the original. The scripts find it beside the driver; `SPLIT_INLINE` names another location.

```sh
#!/bin/sh
# examples/cli/85-split-inline.sh                                     -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# `split_inline` turns a header with in-class member definitions into the
# shape specgen reads: the class body declaration-only, the definitions after
# the class inside its own namespace block. Two conversions: a header already
# in specgen form, whose `//!` docblocks travel with the definitions under
# `--move-comments`; and a plain class template, whose definitions take the
# template heads, qualifier and `inline` an out-of-line definition needs.
#
# Both outputs equal the expected files of the tool's own cases
# (tests/split_inline/cases), which is what the examples.parity cases check.
. "$(dirname -- "$0")/env.sh"

if [ ! -x "$SPLIT_INLINE" ]; then
    printf 'split_inline not found at %s\n' "$SPLIT_INLINE" >&2
    printf 'Run `make install-release`, or set SPLIT_INLINE to a build-tree binary.\n' >&2
    exit 1
fi

OUT=$(out_dir 85-split-inline)
cd "$REPO_ROOT"

# A specgen-form header with in-class definitions: the docblocks move to the
# definitions, under the `\rSec3` section their `\ref` group names.
"$SPLIT_INLINE" tests/corpus/spec_inclass.hpp \
    --no-compile-commands --move-comments \
    -o "$OUT/inclass.hpp"

# A constrained class template, converted without touching its comments.
"$SPLIT_INLINE" tests/split_inline/cases/members/input.hpp \
    --no-compile-commands \
    -o "$OUT/members.hpp"
```

A specgen-form header with two in-class definitions, before and after:

```cpp
// tests/corpus/spec_inclass.hpp                                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (design §3.3 sub-case, §6: in-class-
// defined members and hidden friends). Under one `\rSec3` section, three
// members declared in class-body order exercise the two attachment paths that
// interleave: `front()` is an ordinary out-of-line member (itemdecl from the
// in-class declaration, markup at out-of-line definition — the chain path);
// `size()` is an in-class-defined member (the compiler-bug shape: body and
// `//!` docblock both in the class); `operator==` is a hidden friend (in-class
// by nature, `friend` kept in the itemdecl). All three sit under one
// `\ref{buffer.access}` group, so their itemdescrs must land in the
// `buffer.access` section in class-body order — front(), size(), operator== —
// with the injected in-class items sorted against the out-of-line sibling.
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_INCLASS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_INCLASS_HPP

namespace demo {

class buffer {
  public:
    // \ref{buffer.access}, access
    int& front();

    //! \returns The number of elements.
    int size() const { return count_; }

    //! \returns `true` when the two buffers have equal size.
    friend bool operator==(buffer a, buffer b) { return a.count_ == b.count_; }

  private:
    int count_ = 0;
};

// \rSec3[buffer.access]{Access}

//! \effects Returns a reference to the first element.
int& buffer::front() { return count_; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_INCLASS_HPP
```

```cpp
// tests/corpus/spec_inclass.hpp                                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (design §3.3 sub-case, §6: in-class-
// defined members and hidden friends). Under one `\rSec3` section, three
// members declared in class-body order exercise the two attachment paths that
// interleave: `front()` is an ordinary out-of-line member (itemdecl from the
// in-class declaration, markup at out-of-line definition — the chain path);
// `size()` is an in-class-defined member (the compiler-bug shape: body and
// `//!` docblock both in the class); `operator==` is a hidden friend (in-class
// by nature, `friend` kept in the itemdecl). All three sit under one
// `\ref{buffer.access}` group, so their itemdescrs must land in the
// `buffer.access` section in class-body order — front(), size(), operator== —
// with the injected in-class items sorted against the out-of-line sibling.
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_INCLASS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_INCLASS_HPP

namespace demo {

class buffer {
  public:
    // \ref{buffer.access}, access
    int& front();

    int size() const;

    //! \returns `true` when the two buffers have equal size.
    friend bool operator==(buffer a, buffer b) { return a.count_ == b.count_; }

  private:
    int count_ = 0;
};

// \rSec3[buffer.access]{Access}

//! \effects Returns a reference to the first element.
int& buffer::front() { return count_; }

//! \returns The number of elements.
inline int buffer::size() const { return count_; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_INCLASS_HPP
```

The class template of the tool's own `members` case, converted without touching its comments:

```cpp
// tests/split_inline/cases/members/input.hpp                      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Every member shape the plan's spike covered (docs/plans/split-inline.md),
// in one hermetic header: a constrained class template with a defaulted
// parameter, constructors with mem-initializers, a destructor, a conversion
// function, operators, a static and a virtual member, member templates with
// their own constraints, a partial and an explicit specialization, a nested
// class whose members return nested types, hidden friends, and defaulted and
// deleted members. The `std` declarations are local stand-ins.

#ifndef BEMAN_SPECGEN_SPLIT_INLINE_MEMBERS_INPUT_HPP
#define BEMAN_SPECGEN_SPLIT_INLINE_MEMBERS_INPUT_HPP

namespace std {
template <class T>
constexpr bool is_copy_constructible_v = true;
template <class T, class U>
constexpr bool is_convertible_v = true;
} // namespace std

namespace demo::inner {
inline namespace v1 {

struct nullopt_t {
    explicit constexpr nullopt_t(int);
};

constexpr nullopt_t::nullopt_t(int) {}

template <class T, class Alloc = int>
    requires std::is_copy_constructible_v<T>
class optional {
  public:
    using value_type = T;
    struct iterator {
        T*        p_;
        iterator& operator++();
        T& operator*() const;
    };

    // \ref{optional.ctor}, constructors
    //! \effects Constructs a disengaged optional.
    constexpr optional() noexcept;
    /// Doxygen on a converting constructor; stays with the declaration.
    explicit constexpr optional(const T& v);
    template <class U = T>
        requires std::is_convertible_v<U, T>
    constexpr optional(U&& u);
    optional(const optional&) = default;
    optional(optional&&)      = delete;

    // \ref{optional.dtor}, destructor
    ~optional();

    // \ref{optional.observe}, observers
    [[nodiscard]] constexpr bool has_value() const noexcept;
    constexpr explicit           operator bool() const noexcept;
    iterator                     begin();
    optional&                    self();
    static constexpr int         max_size(int hint = 42, bool strict = false);
    virtual void                 poke();
    friend bool                  operator==(const optional& a, const optional& b) { return a.engaged_ == b.engaged_; }
    template <class U>
    friend bool operator==(const optional& a, const U& b) {
        return a.engaged_ && a.value_ == b;
    }

    // \ref{optional.mod}, modifiers
    void reset() noexcept;
    template <class... Args>
    T& emplace(Args&&... args)
        requires std::is_copy_constructible_v<T>;

  private:
    bool engaged_;
    T    value_;
};

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
auto optional<T, Alloc>::iterator::operator++() -> iterator& {
    ++p_;
    return *this;
}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
T& optional<T, Alloc>::iterator::operator*() const {
    return *p_;
}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
constexpr optional<T, Alloc>::optional() noexcept : engaged_(false), value_() {}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
constexpr optional<T, Alloc>::optional(const T& v) : engaged_(true), value_(v) {}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
template <class U>
    requires std::is_convertible_v<U, T>
constexpr optional<T, Alloc>::optional(U&& u) : engaged_(true), value_(static_cast<T>(u)) {}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
optional<T, Alloc>::~optional() {
    reset();
}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
constexpr bool optional<T, Alloc>::has_value() const noexcept {
    return engaged_;
}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
constexpr optional<T, Alloc>::operator bool() const noexcept {
    return engaged_;
}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
auto optional<T, Alloc>::begin() -> iterator {
    return iterator{&value_};
}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
auto optional<T, Alloc>::self() -> optional& {
    return *this;
}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
constexpr int optional<T, Alloc>::max_size(int hint, bool strict) {
    return hint + (strict ? 0 : 1);
}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
void optional<T, Alloc>::poke() {}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
void optional<T, Alloc>::reset() noexcept {
    engaged_ = false;
}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
template <class... Args>
T& optional<T, Alloc>::emplace(Args&&... args)
    requires std::is_copy_constructible_v<T>
{
    engaged_ = true;
    value_   = T(static_cast<Args&&>(args)...);
    return value_;
}

template <class T, class A>
class optional<T*, A> {
  public:
    bool has_value() const;

  private:
    T* p_ = nullptr;
};

template <class T, class A>
bool optional<T*, A>::has_value() const {
    return p_ != nullptr;
}

template <>
class optional<char, char> {
  public:
    int get() const;
};

inline int optional<char, char>::get() const { return 7; }

class widget {
  public:
    widget();
    explicit widget(int value);
    bool         empty() const;
    void         bump();
    virtual void poke();

  private:
    int value_ = 0;
};

inline widget::widget() : value_(0) {}

inline widget::widget(int value) : value_(value) {}

inline bool widget::empty() const { return value_ == 0; }

inline void widget::bump() { ++value_; }

inline void widget::poke() {}

class gadget : public widget {
  public:
    void poke() override;
};

inline void gadget::poke() { bump(); }

} // namespace v1
} // namespace demo::inner

#endif // BEMAN_SPECGEN_SPLIT_INLINE_MEMBERS_INPUT_HPP
```


<a id="larger-header-example"></a>

# Larger Header Example

`spec_optional.hpp` is the acid-test-shaped corpus header. It exercises the realistic document path: sections, overload grouping, derived constraints and mandates, extracted equivalent-to bodies, exposition-only state, hidden friends, `\seebelow`, and backend differences. Most defects appear here first.

```cpp
// tests/corpus/spec_optional.hpp                                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The acid-test corpus (design §10): a representative subset of
// `beman::optional`'s primary template, reverse-engineered into specgen's
// authoring style, whose generated synopsis is diffed against the draft's
// [optional] synopsis. Modelled on
// bemanproject/optional's include/beman/optional/optional.hpp — not vendored
// (decision hermetic-corpus), so the `std` names below are local stand-ins
// declared just well enough to resolve; only their *resolution* matters to the
// namespace rewriter. Nothing here is ever instantiated, so the bodies need
// only parse.
//
// It exercises every path the tool has: the §10 golden trio — `emplace`
// (Mandates from a static_assert + authored Effects), `value_or` (Mandates +
// `\effects-equiv`), and a converting constructor (a long Constraints list) —
// plus `\merge`d defaulted twins, exposition-only storage, unmarked private
// helpers that must not reach the synopsis, a hidden friend, and a `\seebelow`
// deduced return type.
//
// `nullopt_t`'s constructor carries `\omit` because the draft does not specify
// it: it exists only to make the type non-aggregate and not default
// constructible, which [optional.nullopt]'s prose states directly rather than
// by giving the constructor wording of its own.
//
// The default constructor's body calls `hard_reset()` — an undocumented
// private helper — on purpose, and it is the one thing here that draws a
// finding. Design §9 gives that its *note* severity: the body carries no
// `\effects-equiv`, so nothing the reader sees names `hard_reset`, and the
// wording is followable as written. It is the same helper §9 names in its
// *error* example, in the one position where the error does not apply; the
// note is pinned by `golden.optional_validate`.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_OPTIONAL_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_OPTIONAL_HPP

namespace std {
template <class T>
using remove_cv_t = T;
template <class T>
constexpr bool is_copy_constructible_v = true;
template <class T>
constexpr bool is_trivially_copy_constructible_v = true;
template <class T>
constexpr bool is_trivially_destructible_v = true;
template <class T, class... Args>
constexpr bool is_constructible_v = true;
template <class T, class U>
constexpr bool is_convertible_v = true;
template <class T, class U>
constexpr bool is_same_v = false;
} // namespace std

namespace beman::optional {

struct nullopt_t {
    //! \omit
    explicit constexpr nullopt_t(int) {}
};
inline constexpr nullopt_t nullopt{0};

struct in_place_t {
    explicit in_place_t() = default;
};
inline constexpr in_place_t in_place{};

template <class T>
class optional {
  public:
    using value_type = T;

    // \ref{optional.ctor}, constructors
    constexpr optional() noexcept;
    constexpr optional(nullopt_t) noexcept;

    constexpr optional(const optional& rhs)
        requires std::is_copy_constructible_v<T> && (!std::is_trivially_copy_constructible_v<T>);

    //! \merge
    constexpr optional(const optional&)
        requires std::is_copy_constructible_v<T> && std::is_trivially_copy_constructible_v<T>
    = default;

    template <class... Args>
    constexpr explicit optional(in_place_t, Args&&... args)
        requires std::is_constructible_v<T, Args...>;

    template <class U>
    constexpr explicit(!std::is_convertible_v<U, T>) optional(const optional<U>& rhs)
        requires std::is_constructible_v<T, const U&> && std::is_convertible_v<U, T> && (!std::is_same_v<T, U>) &&
                 (!std::is_constructible_v<T, optional<U>>);

    // \ref{optional.dtor}, destructor
    //! \merge
    constexpr ~optional()
        requires std::is_trivially_destructible_v<T>
    = default;

    // \ref{optional.assign}, assignment
    template <class... Args>
    constexpr T& emplace(Args&&... args);

    // \ref{optional.observe}, observers
    constexpr bool     has_value() const noexcept;
    constexpr const T& operator*() const&;

    template <class U = std::remove_cv_t<T>>
    constexpr std::remove_cv_t<T> value_or(U&& u) const&;

    // \ref{optional.monadic}, monadic operations
    template <class F>
    constexpr auto transform(F&& f) const&;

    // \ref{optional.mod}, modifiers
    constexpr void reset() noexcept;

    //! \returns `true` if both operands are disengaged, or both are engaged
    //! with equal contained values.
    friend constexpr bool operator==(const optional& x, const optional& y) { return x.engaged_ == y.engaged_; }

  private:
    //! \expos
    T value_;
    //! \expos
    bool engaged_ = false;

    constexpr void construct(const T& v);
    constexpr void hard_reset() noexcept;
};

// \rSec3[optional.ctor]{Constructors}

//! \ensures `*this` does not contain a value.
template <class T>
constexpr optional<T>::optional() noexcept {
    hard_reset();
}

//! \also
template <class T>
constexpr optional<T>::optional(nullopt_t) noexcept {}

//! \effects If `rhs` contains a value, direct-non-list-initializes the
//! contained value with `*rhs`.
//! \ensures `has_value()` is equal to `rhs.has_value()`.
template <class T>
constexpr optional<T>::optional(const optional& rhs)
    requires std::is_copy_constructible_v<T> && (!std::is_trivially_copy_constructible_v<T>)
{
    engaged_ = rhs.engaged_;
}

//! \effects Direct-non-list-initializes the contained value with
//! `std::forward<Args>(args)...`.
//! \ensures `has_value()` is `true`.
template <class T>
template <class... Args>
constexpr optional<T>::optional(in_place_t, Args&&... args)
    requires std::is_constructible_v<T, Args...>
{
    engaged_ = true;
}

//! \effects If `rhs` contains a value, direct-non-list-initializes the
//! contained value with `*rhs`.
//! \ensures `has_value()` is equal to `rhs.has_value()`.
template <class T>
template <class U>
constexpr optional<T>::optional(const optional<U>& rhs)
    requires std::is_constructible_v<T, const U&> && std::is_convertible_v<U, T> && (!std::is_same_v<T, U>) &&
             (!std::is_constructible_v<T, optional<U>>)
{
    engaged_ = rhs.has_value();
}

// \rSec3[optional.assign]{Assignment}

//! \effects Destroys any contained value, then direct-non-list-initializes the
//! contained value with `std::forward<Args>(args)...`.
//! \ensures `has_value()` is `true`.
//! \returns A reference to the new contained value.
template <class T>
template <class... Args>
constexpr T& optional<T>::emplace(Args&&... args) {
    static_assert(std::is_constructible_v<T, Args...>);
    engaged_ = true;
    return value_;
}

// \rSec3[optional.observe]{Observers}

//! \returns `true` if and only if `*this` contains a value.
template <class T>
constexpr bool optional<T>::has_value() const noexcept {
    return engaged_;
}

//! \hardexpects `*this` contains a value.
//! \returns A reference to the contained value.
template <class T>
constexpr const T& optional<T>::operator*() const& {
    return value_;
}

//! \effects-equiv
template <class T>
template <class U>
constexpr std::remove_cv_t<T> optional<T>::value_or(U&& u) const& {
    static_assert(std::is_copy_constructible_v<T> && std::is_convertible_v<U, T>);
    return has_value() ? **this : static_cast<std::remove_cv_t<T>>(u);
}

// \rSec3[optional.monadic]{Monadic operations}

//! \seebelow
//! \effects If `*this` contains a value, returns an optional holding the result
//! of invoking `f` with the contained value; otherwise returns an empty
//! optional.
//! \remarks The return type is `remove_cvref_t<invoke_result_t<F, const T&>>`.
template <class T>
template <class F>
constexpr auto optional<T>::transform(F&& f) const& {
    return f(value_);
}

// \rSec3[optional.mod]{Modifiers}

//! \effects-equiv
//! \ensures `has_value()` is `false`.
template <class T>
constexpr void optional<T>::reset() noexcept {
    engaged_ = false;
}

} // namespace beman::optional

#endif // BEMAN_SPECGEN_CORPUS_SPEC_OPTIONAL_HPP
```

```sh
#!/bin/sh
# examples/cli/90-optional.sh                                         -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The acid-test-shaped corpus header, end to end: header to IR, then that
# generated IR to mpark/wg21 markdown. The rendering is deliberately taken from
# the IR this script just generated rather than from the checked-in fixture, so
# the pair of captured files stands as one pipeline rather than two unrelated
# runs -- and both halves still match their goldens, which is what says the
# generated IR and the checked-in IR are the same bytes.
. "$(dirname -- "$0")/env.sh"

OUT=$(out_dir 90-optional)
cd "$REPO_ROOT"

"$SPECGEN" generate --emit-ir tests/corpus/spec_optional.hpp \
    --no-compile-commands \
    --output "$OUT/optional.json"

"$SPECGEN" render --from-ir "$OUT/optional.json" \
    --backend mpark \
    -o "$OUT/optional.md"
```

The markdown below is rendered from the IR that the command above generated, not from a checked-in fixture. Both the IR and the markdown match their golden files, which establishes that the generated IR and the checked-in IR are identical.

````markdown
```cpp
namespace std {
  struct nullopt_t {};
}
```

```cpp
namespace std {
  struct in_place_t {
    explicit in_place_t() = default;
  };
}
```

```cpp
namespace std {
  template<class T>
  class optional {
  public:
    using value_type = T;

    // @[optional.ctor]{- .sref}@, constructors
    constexpr optional() noexcept;
    constexpr optional(nullopt_t) noexcept;

    constexpr optional(const optional& rhs);

    template<class... Args> constexpr explicit optional(in_place_t, Args&&... args);

    template<class U>
    constexpr explicit(!is_convertible_v<U, T>) optional(const optional<U>& rhs);

    // @[optional.assign]{- .sref}@, assignment
    template<class... Args> constexpr T& emplace(Args&&... args);

    // @[optional.observe]{- .sref}@, observers
    constexpr bool has_value() const noexcept;
    constexpr const T& operator*() const&;

    template<class U = remove_cv_t<T>> constexpr remove_cv_t<T> value_or(U&& u) const&;

    // @[optional.monadic]{- .sref}@, monadic operations
    template<class F> constexpr $see below$ transform(F&& f) const&;

    // @[optional.mod]{- .sref}@, modifiers
    constexpr void reset() noexcept;

    friend constexpr bool operator==(const optional& x, const optional& y);

  private:
    T $value$;              // exposition only
    bool $engaged$ = false; // exposition only
  };
}
```

::: wording

## Constructors [optional.ctor]{- .sref} {-}

```cpp
constexpr optional() noexcept;
constexpr optional(nullopt_t) noexcept;
```

[#]{.pnum} *Postconditions*: `*this` does not contain a value.

```cpp
constexpr optional(const optional& rhs);
```

[#]{.pnum} *Constraints*: `is_copy_constructible_v<T>` is `true`.

[#]{.pnum} *Effects*: If `rhs` contains a value, direct-non-list-initializes the contained value with `*rhs`.

[#]{.pnum} *Postconditions*: `has_value()` is equal to `rhs.has_value()`.

```cpp
template<class... Args>
  constexpr explicit optional(in_place_t, Args&&... args);
```

[#]{.pnum} *Constraints*: `is_constructible_v<T, Args...>` is `true`.

[#]{.pnum} *Effects*: Direct-non-list-initializes the contained value with `std::forward<Args>(args)...`.

[#]{.pnum} *Postconditions*: `has_value()` is `true`.

```cpp
template<class U>
  constexpr explicit(!is_convertible_v<U, T>) optional(const optional<U>& rhs);
```

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `is_constructible_v<T, const U&>` is `true`,
- [#.#]{.pnum} `is_convertible_v<U, T>` is `true`,
- [#.#]{.pnum} `is_same_v<T, U>` is `false`,
- [#.#]{.pnum} `is_constructible_v<T, optional<U>>` is `false`.

[#]{.pnum} *Effects*: If `rhs` contains a value, direct-non-list-initializes the contained value with `*rhs`.

[#]{.pnum} *Postconditions*: `has_value()` is equal to `rhs.has_value()`.

:::

::: wording

## Assignment [optional.assign]{- .sref} {-}

```cpp
template<class... Args>
  constexpr T& emplace(Args&&... args);
```

[#]{.pnum} *Mandates*: `is_constructible_v<T, Args...>` is `true`.

[#]{.pnum} *Effects*: Destroys any contained value, then direct-non-list-initializes the contained value with `std::forward<Args>(args)...`.

[#]{.pnum} *Postconditions*: `has_value()` is `true`.

[#]{.pnum} *Returns*: A reference to the new contained value.

:::

::: wording

## Observers [optional.observe]{- .sref} {-}

```cpp
constexpr bool has_value() const noexcept;
```

[#]{.pnum} *Returns*: `true` if and only if `*this` contains a value.

```cpp
constexpr const T& operator*() const&;
```

[#]{.pnum} *Hardened preconditions*: `*this` contains a value.

[#]{.pnum} *Returns*: A reference to the contained value.

```cpp
template<class U = remove_cv_t<T>>
  constexpr remove_cv_t<T> value_or(U&& u) const&;
```

[#]{.pnum} *Mandates*: `is_copy_constructible_v<T>` is `true` and `is_convertible_v<U, T>` is `true`.

[#]{.pnum} *Effects*: Equivalent to:

```cpp
return has_value() ? **this : static_cast<remove_cv_t<T>>(u);
```

:::

::: wording

## Monadic operations [optional.monadic]{- .sref} {-}

```cpp
template<class F>
  constexpr $see below$ transform(F&& f) const&;
```

[#]{.pnum} *Effects*: If `*this` contains a value, returns an optional holding the result of invoking `f` with the contained value; otherwise returns an empty optional.

[#]{.pnum} *Remarks*: The return type is `remove_cvref_t<invoke_result_t<F, const T&>>`.

:::

::: wording

## Modifiers [optional.mod]{- .sref} {-}

```cpp
constexpr void reset() noexcept;
```

[#]{.pnum} *Effects*: Equivalent to:

```cpp
$engaged$ = false;
```

[#]{.pnum} *Postconditions*: `has_value()` is `false`.

```cpp
friend constexpr bool operator==(const optional& x, const optional& y);
```

[#]{.pnum} *Returns*: `true` if both operands are disengaged, or both are engaged with equal contained values.

:::
````
