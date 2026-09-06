# Plan: moving the front end to LLVM 23

**Status:** planned, not started. The pin stays at `22.1` until
[pin-bump](#pin-bump) lands. Everything below was measured on 2026-09-06 against
the LLVM 23 packages on the dev box and against the released clang-format
23.1.0; the reproductions are recorded so a later reader can redo them rather
than trust the numbers.

## Goal

Move `BEMAN_SPECGEN_LLVM_VERSION` from `22.1` to `23.1` — the front end, the
build hints, CI, and the prose that names the version — and end green on the
same 757 tests. Decision [llvm-toolchain-pin](../decisions/llvm-toolchain-pin.md)
already says a move is one deliberate flag; this is the first exercise of it, so
the plan is also a check that the flag is all it costs.

LLVM 23.1.0 released on 2026-08-25 (`llvmorg-23.1.0`). It is the first release
of the 23 line, so the pin's target is `23.1`, not `23.0` — the `23.0.0` the
`llvm-23` packages carry on this box is apt.llvm.org's development snapshot
(`1:23~++20260614083101+db210c5e3364`, 2026-06-14), from the unversioned
`llvm-toolchain-questing` channel rather than a release channel.

## What the port actually costs

A probe build configured and built the whole tree against the installed LLVM 23:

```sh
uv run cmake --preset gcc-release -B <scratch>/build-llvm23 \
  -DBEMAN_SPECGEN_LLVM_VERSION=23.0 \
  -DClang_DIR=/usr/lib/llvm-23/lib/cmake/clang
uv run cmake --build <scratch>/build-llvm23 -j8
uv run ctest --test-dir <scratch>/build-llvm23 -j8 --output-on-failure
```

Configuration succeeded untouched: `find_package(Clang 23.0)` resolved,
`LLVMConfig`'s transitive `FindFFI`/`FindLibEdit` probes ran, and the
`clang-cpp` + `LLVM` shared link held. Nothing in `CMakeLists.txt` or
`src/beman/specgen/frontend/CMakeLists.txt` needed changing beyond the version
string. Two things came out of the build and test run.

**One compile error, one call site.** `ASTContext::getRawCommentForDeclNoCache`
is gone in 23:

```text
frontend.cpp:894:34: error: 'class clang::ASTContext' has no member named
    'getRawCommentForDeclNoCache'; did you mean 'getRawCommentNoCache'?
```

The rename is part of a widening: 23 keys the raw-comment lookups on a
`RawCommentLookupKey`, which is `llvm::PointerUnion<const Decl*, const
MacroInfo*>`, so macros can carry attached comments too. Because
`PointerUnion` is implicitly constructible from either alternative, the call
site needs the *name* changed and nothing else — no cast, no key construction.
Replacing the one token compiled the front end clean, zero errors, zero new
warnings under the project's flags. `getRawCommentForAnyRedecl` moved to the
same key type but specgen does not call it.

**Three failing goldens, one behavioral difference.** 754 of 757 passed.
`golden.foreign_include`, `golden.foreign_include.singlepass`, and
`golden.foreign_include.eastconst` are the same finding seen three ways, and the
whole of it is two occurrences of one string in
`tests/golden/foreign_include/expected.json`:

```diff
-"text": "requires(const Impl& impl) { impl.step(detail::eval); }"
+"text": "requires(const Impl & impl) { impl.step(detail::eval); }"
```

This is clang-format, not the AST. specgen hands the derived *Constraints*
wording a bare requires-expression — no surrounding declaration — to
`format_code` (`frontend.cpp:457`) under the draft `FormatStyle`, whose
`PointerAlignment` is `PAS_Left` (`frontend.cpp:437`). clang-format 23 parses
the `&` in that context-free fragment as a binary operator and spaces it
accordingly; 21 and 22 read it as a declarator. Reduced to the fragment alone:

```sh
printf 'requires(const Impl& impl) { impl.step(detail::eval); }\n' > /tmp/f.cpp
uvx --from clang-format==22.1.5 clang-format \
  --style='{BasedOnStyle: LLVM, PointerAlignment: Left}' /tmp/f.cpp
#   requires(const Impl& impl) { impl.step(detail::eval); }
uvx --from clang-format==23.1.0 clang-format \
  --style='{BasedOnStyle: LLVM, PointerAlignment: Left}' /tmp/f.cpp
#   requires(const Impl & impl) { impl.step(detail::eval); }
```

It is present in the **released** 23.1.0, not only in the dev snapshot the
probe built against, so it is not something to wait out. It is also specific to
the bare fragment: the same requires-clause inside a declaration still formats
as `Impl&` under 23.1.0, which is both the diagnosis and the shape of a fix —
see [constraints-fragment-format](#constraints-fragment-format).

**What did not move.** `golden.include_path_parse_error` pins Clang's own fatal
diagnostic text, caret diagram included, and `tests/golden/CMakeLists.txt`
warns that a Clang upgrade rewording it moves the golden. 23 did not reword it;
the case passed. No synopsis, no signature, no `noexcept` or drift cross-check,
no validation finding, and no `examples.` capture moved. The examples pipeline
therefore needs no re-capture, and the test count stays 757.

## Steps

Each is one PR-sized change; the stage number is reading order only.
Cross-reference by the slug, never the number.

### llvm-23-install

Stage 1. Get a released LLVM/Clang **23.1** development install onto the dev
box. The `llvm-23` packages currently installed are the June dev snapshot
(23.0.0), and `find_package(Clang 23.1)` rejects them by design — the pin
cannot move before this does. Either add the `llvm-toolchain-questing-23`
apt.llvm.org line beside the existing `-20`/`-21` ones and install
`llvm-23-dev` + `libclang-23-dev` from it, or unpack the release tarball the
way CI does. Confirm before going further:

```sh
grep LLVM_VERSION_STRING <prefix>/include/llvm/Config/llvm-config.h  # 23.1.x
uv run cmake --preset gcc-release -B <scratch>/b23 \
  -DBEMAN_SPECGEN_LLVM_VERSION=23.1 -DClang_DIR=<prefix>/lib/cmake/clang
```

Re-run the probe from [What the port actually costs](#what-the-port-actually-costs)
against 23.1 and confirm the finding list is still exactly the one above.
23.1.0 is three months of fixes past the snapshot; a *shorter* list is the
likely outcome and would shrink [constraints-fragment-format](#constraints-fragment-format)
or delete it, but the clang-format reduction above says the fragment spacing at
least survives.

### raw-comment-lookup-key

Stage 2. Change `frontend.cpp:894` to `getRawCommentNoCache`, and update the
comment two lines above it, which names `getRawCommentForDeclNoCacheImpl` while
explaining the `;{}#@` anchor rule. Note in that comment that the argument is
now a `RawCommentLookupKey` the `const Decl*` converts into, so the next reader
does not think the conversion is accidental.

This does not build against 22 — the name does not exist there — so it is the
step that actually crosses the version boundary and belongs in the same commit
as [pin-bump](#pin-bump), or immediately before it, depending on
[llvm-22-support-window](#llvm-22-support-window).

### constraints-fragment-format

Stage 3. Settle the derived-Constraints spacing, blocked on
[constraints-fragment-spelling](#constraints-fragment-spelling). If the answer
is to restore `Impl&`, the fix is to give the fragment a declaration context
before formatting and strip it afterwards — verified against 23.1.0:

```sh
printf 'auto probe() requires requires(const Impl& impl) { impl.step(detail::eval); };\n' \
  | uvx --from clang-format==23.1.0 clang-format \
      --style='{BasedOnStyle: LLVM, PointerAlignment: Left}'
# auto probe()
#   requires requires(const Impl& impl) { impl.step(detail::eval); };
```

`frontend.cpp` already owns this idiom: `qualifier_style()` formats through
sentinels and calls `recover_sentinels` (`frontend.cpp:2536`, `:2580`). A
context wrapper is the same shape and belongs beside it, not as a special case
inside `format_code`, which other callers hand well-formed declarations to.

Taste-sensitive output, so the wording gets a first cut and sign-off before the
commit (`AGENTS.md`). If the answer is to accept 23's spelling instead, the step
is `make goldens` for the three `foreign_include` cases and a note in
`tests/golden/CMakeLists.txt` saying which LLVM the spelling tracks.

### pin-bump

Stage 4. Move the pin and every place that writes the version down. The
default in `CMakeLists.txt:125` is the only functional one; the rest are hints
and prose, and they drift the moment one is missed:

| File | What it says |
| --- | --- |
| `CMakeLists.txt:106,121,125` | the default, the search-hint comment, and the worked `-D` example |
| `Makefile:12,16,17` | `CLANG_DIR?=` default and the same worked example |
| `AGENTS.md:29` | default in the build-and-verify section |
| `README.md:78` | default |
| `CONTRIBUTING.md:15,54` | prerequisite line and the cache-variable table |
| `docs/building.md:22` | default |
| `docs/architecture.md:123,129` | default and the worked `-D` example |
| `docs/user-guide.md:24-27` | "requires LLVM/Clang 22's development install" |
| `docs/decisions/llvm-toolchain-pin.md:17,22,48,56` | Context, Decision, Consequences |
| `docs/acid-test.md:77` | the recorded `-DClang_DIR=` in the recipe, and the "LLVM/Clang 22 front end" line |
| `tests/golden/CMakeLists.txt:973` | "(decision llvm-toolchain-pin: LLVM 22)" beside `include_path_parse_error` |

Three of those — `CMakeLists.txt:121`, `Makefile:16`, and
`docs/architecture.md:129` — use `-DBEMAN_SPECGEN_LLVM_VERSION=23.0` as the
worked example of *moving* the pin. Once 23.1 is the pin they read as
instructions to stay put, so each needs a new counter-example, not a
search-and-replace.

`docs/decisions/llvm-toolchain-pin.md` needs more than a search-and-replace: its
Context cites the `getRawCommentForDeclNoCache` rename as the *hypothetical*
that motivates the pin. That prediction came true, so the record should say so —
appending to it that the move cost one identifier and one golden is the
evidence that the pin did its job, and is worth more than the hypothetical it
replaces.

Optionally add `cmake/clang-23-toolchain.cmake` next to `clang-22-toolchain.cmake`
for `make TOOLCHAIN=clang-23`; it is a four-line copy and independent of
everything else here.

### ci-llvm-23

Stage 5. `.github/workflows/ci_tests.yml` installs the LLVM release tarball in
three places (two build lanes at `:76`, coverage at `:116`). Each needs the tag,
the archive name, the unpack directory, the `libclang-cpp.so.NN.N` symlink
names, and the `-DClang_DIR=` in the Configure step moved together —
`llvmorg-23.1.0`, `LLVM-23.1.0-Linux-X64.tar.xz`, `libclang-cpp.so.23.1`.
Confirm the asset name on the release before editing; it is the one thing here
that cannot be checked locally.

Two things to watch rather than assume:

- The official release binaries are the **no-RTTI** build, so CI is the only
  lane that exercises the `-fno-rtti` branch in
  `src/beman/specgen/frontend/CMakeLists.txt`. The dev box's apt packages build
  with RTTI on, so a 23 RTTI change would show up here first and nowhere else.
- The `clang` lanes take their compiler from the same unpacked tree via
  `/usr/local/bin/clang++`, so this bumps the *host* compiler for those lanes
  from Clang 22 to Clang 23 at the same time. If a lane fails, separate the two
  before diagnosing.

### clang-format-rev

Stage 6. Bump `.pre-commit-config.yaml`'s `mirrors-clang-format` rev from
`v22.1.5` to the 23.1 rev, so the formatter that polices the tree matches the
one linked into the front end. **This is independent of every step above and
costs nothing**: clang-format 22.1.5 and 23.1.0 both report the 136 tracked
`.hpp`/`.cpp` files (vendor excluded) already conformant, so nothing reformats.

```sh
uvx --from clang-format==23.1.0 clang-format --dry-run \
  $(git ls-files '*.hpp' '*.cpp' | grep -v '^vendor/')   # silent
```

That matters because `tests/corpus/*.hpp` are clang-format-controlled and the
golden diagnostics pin `file:line` — a reformat here would move goldens for
reasons unrelated to the port. It does not, so this step can land first, alone,
and de-risk the rest. Land it separately rather than folding it into
[pin-bump](#pin-bump), so that if a corpus header ever *does* move under a
future formatter, the churn is not tangled with a version bump.

## Open questions

### [constraints-fragment-spelling](#constraints-fragment-spelling)

**Question:** in derived *Constraints* wording, is the requires-expression
spelled `requires(const Impl& impl)` or `requires(const Impl & impl)` under
LLVM 23? **Status:** OPEN.

`Impl&` is the draft's own spelling and what `PAS_Left` exists to produce; the
project sets it deliberately (`frontend.cpp:436-437` says the reference binds
to the type, not the declarator). `Impl & impl` reads as an expression, which
is exactly what clang-format 23 mistook it for. The wording is worse, and it is
worse for a reason that is a clang-format defect rather than a change of
opinion — which argues for restoring it and for reporting the fragment case
upstream. Against: a wrapper is machinery specgen carries forever to work
around one release's parse, and the sentinel idiom it copies is already the
fiddliest corner of the front end.

The answer decides [constraints-fragment-format](#constraints-fragment-format)
entirely: restore means a context wrapper and unchanged goldens, accept means
`make goldens` and a comment.

### [llvm-22-support-window](#llvm-22-support-window)

**Question:** after the move, does the tree still build against LLVM 22?
**Status:** OPEN, leaning no.

[raw-comment-lookup-key](#raw-comment-lookup-key) is a hard break — the 23 name
does not exist in 22 and the 22 name does not exist in 23 — so keeping both
means a version `#if` in `frontend.cpp`, the first one in the file. Decision
[llvm-toolchain-pin](../decisions/llvm-toolchain-pin.md) is written for a
single pinned version and says a mismatched `Clang_DIR` is *rejected*, which
reads as one version at a time; the pin's whole point is that the front end is
written against one LLVM. Supporting a window would be a change to that
decision, not an application of it, and it should be recorded as one if it is
wanted.

What makes the question live at all is that the packaged LLVM on a given box
lags: a contributor on a distro that ships 22 cannot build at all after
[pin-bump](#pin-bump). Whether that is acceptable depends on whether the
answer is "install the release tarball, as CI does", which is the current
answer for everyone on a distro that ships 21.
