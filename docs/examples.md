- [CLI Coverage](#cli-coverage)
- [General Command Information](#general-command-information)
- [Minimal Header To IR](#minimal-header-to-ir)
- [Render-Only IR Examples](#render-only-ir-examples)
- [Mpark Paper Mode](#mpark-paper-mode)
- [Fragment Output](#fragment-output)
- [Validation](#validation)
- [Explicit Include Path](#explicit-include-path)
- [Declaration Interleave](#declaration-interleave)
- [Larger Header Example](#larger-header-example)

<!-- markdownlint-disable -->

Every command below is a script in [`examples/cli/`](../examples/cli), and every output below is what that script printed. Both are transcluded from the repository, so neither is retyped here and neither can drift from the other without a test noticing.

To run them you need the binary. `make install-release` puts it at `.install/bin/specgen`, which is where the scripts look; set `SPECGEN` to point them somewhere else, such as a build tree. There is no build of specgen without the Clang front end — the executable is only produced when `BEMAN_SPECGEN_ENABLE_CLANG` is on — so every command here works on any binary you have.

```sh
make install-release          # or: export SPECGEN=<build tree>/tools/specgen/specgen
examples/cli/run-all.sh       # re-capture everything shown below
```

`run-all.sh` rewrites the checked-in output, and `ctest -R examples.` checks it. Most of those checks compare a captured file against the golden suite, which already pins that golden to what specgen printed; the rest re-run a script and compare what comes out. So the output here is not an illustration of what specgen does. It is what specgen did.


<a id="cli-coverage"></a>

# CLI Coverage

| Command or option                 | Example section                             |
|--------------------------------- |------------------------------------------- |
| `--help`                          | General command information                 |
| `generate <header>` (single pass) | Minimal header to IR                        |
| `generate --emit-ir`              | Minimal header to IR                        |
| `--no-compile-commands`           | Minimal header to IR                        |
| `-o`, `--output`                  | Minimal header to IR, Larger header example |
| `-- <clang args>...`              | Explicit include path                       |
| `--compile-commands <dir>`        | Explicit include path                       |
| `dump-decls`                      | Declaration interleave                      |
| `render --from-ir <file>`         | Render-only IR examples                     |
| `render --from-ir -`              | Minimal header to IR                        |
| `--backend latex`, `mpark`, `org` | Render-only IR examples                     |
| `--validate`                      | Validation                                  |
| `--paper`                         | Mpark paper mode                            |
| `--split`, `--root`               | Fragment output                             |


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
```

Every command takes `--help`, and all four print the same usage text, so one of them is shown here.

```text
usage: specgen <command> [options]

commands:
  render      render IR JSON to wording fragments
  generate    render a C++ header to wording fragments
  dump-decls  print a header's decl/comment interleave (debug)

dump-decls options:
  <header>                  the header to parse
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
  <header>                  the header to parse; with none, parse a stock
			     snippet as a link-proving smoke check
  --emit-ir                 emit the document tree (design §3.2) as IR JSON
			     for a later `render --from-ir`, instead of
			     rendering wording here
  --backend <name>          latex (default), mpark, or org
  --validate                run the wording validators before rendering; a
			     finding at error severity aborts the render
			     (exit 1) instead
  --paper                   wrap the fragment in an `::: add` editing-instruction
			     div and number its paragraphs as added (mpark only)
  --split <dir>             write one fragment per top-level section into <dir>,
			     named from its stable name (optional.ctor.tex), and
			     list the paths written on standard output
  --root <name>             name the fragment holding the nodes outside every
			     section (--split only); derived from the sections'
			     common stable-name prefix when omitted
  -o, --output <file>       write here instead of standard output
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
  --from-ir <file>    IR JSON to read; "-" for standard input (required)
  --backend <name>    latex (default), mpark, or org
  --validate          run the wording validators before rendering; a finding
		       at error severity aborts the render (exit 1) instead
  --paper             wrap the fragment in an `::: add` editing-instruction div
		       and number its paragraphs as added (mpark only)
  --split <dir>       write one fragment per top-level section into <dir>,
		       named from its stable name (optional.ctor.tex), and
		       list the paths written on standard output
  --root <name>       name the fragment holding the nodes outside every
		       section (--split only); derived from the sections'
		       common stable-name prefix when omitted
  -o, --output <file> write here instead of standard output

general:
  -h, --help          show this message
  --version           show the version
```


<a id="minimal-header-to-ir"></a>

# Minimal Header To IR

The smallest useful header example is `spec_widget.hpp`. It has class synopsis groups, two sections, attached out-of-line definitions, and ordinary description elements. Small, but enough.

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
require_tier_b

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

The IR lands in `widget.json`; it is the same document the golden suite pins, so it is checked rather than shown.

The second command is the whole pipeline in one process. `generate` without `--emit-ir` runs the front end and a backend in a single pass: the IR is built, validated if asked, rendered, and never written down.

```latex
\begin{codeblock}
class @\libglobal{widget}@ {
public:
  // \ref{widget.cons}, constructors
  widget();
  explicit widget(int value);

  // \ref{widget.observers}, observers
  bool empty() const;
};
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

The third command is the same wording reached the long way, as two processes with the IR passed between them on a pipe. It is byte for byte the file above — `render` and `generate` share the half of the driver that turns a document into wording, so there is nowhere for the two routes to differ:

```latex
\begin{codeblock}
class @\libglobal{widget}@ {
public:
  // \ref{widget.cons}, constructors
  widget();
  explicit widget(int value);

  // \ref{widget.observers}, observers
  bool empty() const;
};
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


<a id="render-only-ir-examples"></a>

# Render-Only IR Examples

These render the small hand-written `value_or` IR fixture. It is useful for backend comparison because the input is identical and only the serializer changes.

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
# the three outputs worth reading side by side. Needs no Clang: `render` is
# clang-free, so this script runs on every lane.
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

```markdown
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
```

org, for the `wg21org` exporter:

```org
#+begin_itemdecl
template<class U = remove_cv_t<T>> constexpr remove_cv_t<T> value_or(U&& v) const &;
#+end_itemdecl

/Mandates/: ~is_copy_constructible_v<T>~ is ~true~ and ~is_convertible_v<U, T>~ is ~true~.

/Effects/: Equivalent to:

#+begin_codeblock
return has_value() ? **this : static_cast<remove_cv_t<T>>(std::forward<U>(v));
#+end_codeblock
```


<a id="mpark-paper-mode"></a>

# Mpark Paper Mode

Paper mode is mpark-only. It wraps the fragment in an editing-instruction div and numbers added paragraphs as `x`, `x+1`, and so on.

```sh
#!/bin/sh
# examples/cli/40-paper-mode.sh                                       -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Paper mode is mpark-only: it wraps the fragment in an editing-instruction div
# and numbers added paragraphs x, x+1, and so on. No other backend has it.
. "$(dirname -- "$0")/env.sh"

OUT=$(out_dir 40-paper-mode)
cd "$REPO_ROOT"

"$SPECGEN" render --from-ir tests/golden/paper_mode/input.json \
    --backend mpark \
    --paper \
    -o "$OUT/paper-mode.md"
```

```markdown
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

[x]{.pnum} *Effects*: Resets the gadget.

:::

:::
```


<a id="fragment-output"></a>

# Fragment Output

`--split` writes one file per top-level stable-name section and prints a manifest in document order. The same IR goes through all three backends; only the extension and the rendered syntax differ. Consumers should use the manifest, in document order.

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

The org run names its root fragment explicitly, which is why its manifest opens with `optional.syn.org` where the other two derive `optional` from the sections' common prefix:

```text
wording/optional.syn.org
wording/optional.ctor.org
wording/optional.assign.org
wording/optional.observe.org
wording/optional.monadic.org
wording/optional.mod.org
```

One fragment from each backend, the same section three ways:

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
\tcode{is_copy_constructible_v<T>} is \tcode{true} and \tcode{is_trivially_copy_constructible_v<T>} is \tcode{false}.

\pnum
\effects
If \tcode{rhs} contains a value, direct-non-list-initializes the contained value with \tcode{*rhs}.

\pnum
\ensures
\tcode{has_value()} is equal to \tcode{rhs.has_value()}.
\end{itemdescr}

\indexlibraryctor{optional}%
\begin{itemdecl}
template<class... Args> constexpr explicit optional(in_place_t, Args&&... args);
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
\item \tcode{is_constructible_v<T, const U&>} is \tcode{true},
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

```markdown
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

[#]{.pnum} *Constraints*: `is_copy_constructible_v<T>` is `true` and `is_trivially_copy_constructible_v<T>` is `false`.

[#]{.pnum} *Effects*: If `rhs` contains a value, direct-non-list-initializes the contained value with `*rhs`.

[#]{.pnum} *Postconditions*: `has_value()` is equal to `rhs.has_value()`.

```cpp
template<class... Args> constexpr explicit optional(in_place_t, Args&&... args);
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
```

```org
#+begin_codeblock
struct nullopt_t {};
#+end_codeblock

#+begin_codeblock
struct in_place_t {
  explicit in_place_t() = default;
};
#+end_codeblock

#+begin_codeblock
template<class T>
class optional {
public:
  using value_type = T;

  // \ref{optional.ctor}, constructors
  constexpr optional() noexcept;
  constexpr optional(nullopt_t) noexcept;

  constexpr optional(const optional& rhs)
    requires is_copy_constructible_v<T> && (!is_trivially_copy_constructible_v<T>);

  template<class... Args>
  constexpr explicit optional(in_place_t, Args&&... args)
    requires is_constructible_v<T, Args...>;

  template<class U>
  constexpr explicit(!is_convertible_v<U, T>) optional(const optional<U>& rhs)
    requires is_constructible_v<T, const U&> && is_convertible_v<U, T> &&
	     (!is_same_v<T, U>) && (!is_constructible_v<T, optional<U>>);

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
  T @\exposidnc{value}@;            // exposition only
  bool @\exposidnc{engaged}@ = false; // exposition only
};
#+end_codeblock
```


<a id="validation"></a>

# Validation

Validators run in the normal render path.

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

A clean document renders as usual and says nothing. A finding at error severity is reported on standard error, the render is skipped, and the exit status is 1 — which is why the script has to catch it rather than let it abort:

```text
specgen: widget/synopsis: error: `resize` is declared in the synopsis but is not described, `\omit`ted, `\merge`d, or defaulted
specgen: widget/synopsis: error: the description of `reserve` is routed to [widget.nowhere], which is not a section in this document
specgen: widget/synopsis: error: the description of `shrink` is routed to no section: it is written in the class body under no `\ref` group and carries no `\at`
```

`generate` reports its own findings while still emitting IR for the header it parsed:

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
require_tier_b

OUT=$(out_dir 65-diagnostics)

(
    cd "$REPO_ROOT/tests/corpus"
    "$SPECGEN" generate --emit-ir spec_diagnostics.hpp \
	--no-compile-commands \
	-o "$OUT/diagnostics.json" 2> "$OUT/diagnostics.diag"
)
```

```text
spec_diagnostics.hpp:63: warning: duplicate \effects element; both kept
spec_diagnostics.hpp:81: note: \effects appears after \remarks; output is canonicalized
spec_diagnostics.hpp:85: error: unknown tag \effect
spec_diagnostics.hpp:88: warning: malformed \rSec marker: digits: value out of range (comment offset 8)
spec_diagnostics.hpp:90: warning: malformed \rSec marker: expected '{' (comment offset 26)
spec_diagnostics.hpp:94: warning: unrecognized section header [gadget.mod]; use \rSec<depth>[gadget.mod]{title}
```


<a id="explicit-include-path"></a>

# Explicit Include Path

A header that includes another needs an include directory, and there are two ways to say so: after a bare `--`, or in a compilation database.

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
require_tier_b

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

Both produce the same IR, byte for byte, and both are compared against the same golden. A difference between them would be a real difference rather than two files that merely happen to agree.


<a id="declaration-interleave"></a>

# Declaration Interleave

`dump-decls` prints the declaration/comment event stream the front end builds the document from. Useful when a section is empty and should not be.

```sh
#!/bin/sh
# examples/cli/80-dump-decls.sh                                       -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# `dump-decls` prints the declaration/comment event stream the front end builds
# the document from. Useful when a section is empty and should not be.
. "$(dirname -- "$0")/env.sh"
require_tier_b

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


<a id="larger-header-example"></a>

# Larger Header Example

`spec_optional.hpp` is the acid-test-shaped corpus header. It exercises the realistic document path: sections, overload grouping, derived constraints and mandates, extracted equivalent-to bodies, exposition-only state, hidden friends, `\seebelow`, and backend differences. Most bugs show up here first.

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
require_tier_b

OUT=$(out_dir 90-optional)
cd "$REPO_ROOT"

"$SPECGEN" generate --emit-ir tests/corpus/spec_optional.hpp \
    --no-compile-commands \
    --output "$OUT/optional.json"

"$SPECGEN" render --from-ir "$OUT/optional.json" \
    --backend mpark \
    -o "$OUT/optional.md"
```

The markdown below is rendered from the IR the command above generated, not from a checked-in fixture — and both halves match their goldens, which is what says the generated IR and the checked-in IR are the same bytes.

```markdown
```cpp
struct nullopt_t {};
```

```cpp
struct in_place_t {
  explicit in_place_t() = default;
};
```

```cpp
template<class T>
class optional {
public:
  using value_type = T;

  // @[optional.ctor]{- .sref}@, constructors
  constexpr optional() noexcept;
  constexpr optional(nullopt_t) noexcept;

  constexpr optional(const optional& rhs)
    requires is_copy_constructible_v<T> && (!is_trivially_copy_constructible_v<T>);

  template<class... Args>
  constexpr explicit optional(in_place_t, Args&&... args)
    requires is_constructible_v<T, Args...>;

  template<class U>
  constexpr explicit(!is_convertible_v<U, T>) optional(const optional<U>& rhs)
    requires is_constructible_v<T, const U&> && is_convertible_v<U, T> &&
	     (!is_same_v<T, U>) && (!is_constructible_v<T, optional<U>>);

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
  T $value$;            // exposition only
  bool $engaged$ = false; // exposition only
};
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

[#]{.pnum} *Constraints*: `is_copy_constructible_v<T>` is `true` and `is_trivially_copy_constructible_v<T>` is `false`.

[#]{.pnum} *Effects*: If `rhs` contains a value, direct-non-list-initializes the contained value with `*rhs`.

[#]{.pnum} *Postconditions*: `has_value()` is equal to `rhs.has_value()`.

```cpp
template<class... Args> constexpr explicit optional(in_place_t, Args&&... args);
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
template<class... Args> constexpr T& emplace(Args&&... args);
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
template<class U = remove_cv_t<T>> constexpr remove_cv_t<T> value_or(U&& u) const&;
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
template<class F> constexpr $see below$ transform(F&& f) const&;
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
```
