# Plan: moving the front end to LLVM 23

**Status:** in progress, nothing blocked. [llvm-23-install](#llvm-23-install)
and [constraints-fragment-format](#constraints-fragment-format) are done and
both questions below are decided, so the remaining stages —
[raw-comment-lookup-key](#raw-comment-lookup-key), [pin-bump](#pin-bump),
[ci-llvm-23](#ci-llvm-23), [clang-format-rev](#clang-format-rev) — are
mechanical. The pin stays at
`22.1` until [pin-bump](#pin-bump) lands. Everything below was measured on
2026-09-06 against **LLVM 23.1.1** installed on the dev box from apt.llvm.org;
the reproductions are recorded so a later reader can redo them rather than trust
the numbers.

## Goal

Move `BEMAN_SPECGEN_LLVM_VERSION` from `22.1` to `23.1` — the front end, the
build hints, CI, and the prose that names the version — and end green on the
same 757 tests. Decision [llvm-toolchain-pin](../decisions/llvm-toolchain-pin.md)
already says a move is one deliberate flag; this is the first exercise of it, so
the plan is also a check that the flag is all it costs.

LLVM 23.1.0 released on 2026-08-25 (`llvmorg-23.1.0`), so the pin's target is
`23.1`, not `23.0`: `23.0.0` is the development trunk, not the release line. The
dev box now carries `23.1.1` from apt.llvm.org's 23 channel
(`1:23.1.1~++20260905084250+f603629d9ec7`), which
`find_package(Clang 23.1)` accepts — `ClangConfigVersion.cmake` matches on
major.minor, so the pin tracks the release *line* and point releases arrive
without touching it. The same is already true underneath the current pin: `22.1`
resolves today's `22.1.8` exactly as it resolved `22.1.5`.

## What the port actually costs

A probe build configured and built the whole tree against the installed LLVM 23:

```sh
uv run cmake --preset gcc-release -B <scratch>/build-llvm23 \
  -DBEMAN_SPECGEN_LLVM_VERSION=23.1 \
  -DClang_DIR=/usr/lib/llvm-23/lib/cmake/clang
uv run cmake --build <scratch>/build-llvm23 -j8
uv run ctest --test-dir <scratch>/build-llvm23 -j8 --output-on-failure
```

It was run twice, three months of the 23 branch apart — first against the
`23.0.0` trunk snapshot the box carried before the release, then against
`23.1.1` — and produced the same two findings both times, character for
character. Nothing below is an artifact of a pre-release compiler.

Configuration succeeded untouched: `find_package(Clang 23.1)` resolved,
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
S='{BasedOnStyle: LLVM, PointerAlignment: Left}'
/usr/lib/llvm-22/bin/clang-format --style="$S" /tmp/f.cpp   # 22.1.8
#   requires(const Impl& impl) { impl.step(detail::eval); }
/usr/lib/llvm-23/bin/clang-format --style="$S" /tmp/f.cpp   # 23.1.1
#   requires(const Impl & impl) { impl.step(detail::eval); }
```

Reproduced identically on the released 23.1.0 wheel
(`uvx --from clang-format==23.1.0`), so it is neither a packaging artifact nor
something to wait out. It is also specific to the bare fragment: the same
requires-clause inside a declaration still formats as `Impl&` under 23.1, which
is both the diagnosis and the shape of a fix — see
[constraints-fragment-format](#constraints-fragment-format).

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

Stage 1. **Done** (2026-09-06). `llvm-23-dev` and `libclang-23-dev` 23.1.1 are
installed from apt.llvm.org's 23 channel, at the usual
`/usr/lib/llvm-23/lib/cmake/clang`; `find_package(Clang 23.1)` resolves them
and reports `LLVM 23.1.1`. The probe in
[What the port actually costs](#what-the-port-actually-costs) was re-run against
them and the finding list is unchanged: one compile error, three goldens, the
same two-character diff. The measurements the rest of this plan rests on are
therefore against a released compiler, and the later steps carry no residual
"confirm this against the real 23.1" caveat.

The one thing this step did *not* settle is CI, which takes LLVM from the
official release tarball rather than from Debian packages — a different build
of the same version, notably without RTTI. See [ci-llvm-23](#ci-llvm-23).

### raw-comment-lookup-key

Stage 2. **Done** (2026-09-06), in the same commit as
[pin-bump](#pin-bump) — the rename does not compile against 22, so a separate
commit would build against nothing. `attached_raw_comment` calls
`getRawCommentNoCache`; the comment above it names the new `…NoCacheImpl`
spelling of the anchor rule, says the argument is a `RawCommentLookupKey` the
`const Decl*` converts into rather than a coincidence, and records why the
lookup stays the NoCache one — see the log under
[llvm-22-support-window](#llvm-22-support-window) for the alternative that
compiles on both and is nonetheless wrong.

### constraints-fragment-format

Stage 3. **Done** (2026-09-06), per
[constraints-fragment-spelling](#constraints-fragment-spelling): `Impl&` is
restored rather than the goldens regenerated.

`format_expr_fragment` (`frontend.cpp`, beside `qualifier_style()`) formats the
fragment as a variable initializer — `auto beman_specgen_expr = <expr>;` — and
takes the wrapper off again by its own fixed length. An initializer and not a
requires-clause because *any* expression is a valid initializer, and this path
(`expr_code_rewritten`, reached only from `phrase_conjunct`) carries Mandates
conjuncts lifted from `static_assert` conditions as well as Constraints
conjuncts, which a requires-clause grammar need not admit. `ColumnLimit = 0`,
already set by `qualifier_style()` for the qualifier fixer's sake, is what makes
the round trip safe: with no limit clang-format honours the input's own line
breaks, so the added prefix reflows nothing after it. If the wrapper does not
come back intact the fragment is formatted bare, which is the pre-existing
behavior, so `format_code`'s degrade-to-input remains the only failure mode.

Measured both ways before committing:

- **LLVM 22.1.8, the current pin: 757/757, not one golden byte moved.** The
  wrapper is transparent on the LLVM clang-format got this right on, which is
  why this step could land ahead of [pin-bump](#pin-bump) instead of behind it.
- **LLVM 23.1.1: 757/757.** The three `foreign_include` cases pass with the
  goldens unchanged — the wording is `const Impl&` again.

Checked against every derived conjunct the corpus actually produces (18
distinct fragments across all goldens) plus multi-line, disjunction,
negation, and `T const&` shapes: under both toolchains the wrapped result
equals the LLVM 22 bare result in every case.

### pin-bump

Stage 4. **Done** (2026-09-06). `BEMAN_SPECGEN_LLVM_VERSION` is `23.1`, and a
clean `cmake --preset gcc-release` with no `-DClang_DIR` at all now reports
`Clang front end (LLVM 23.1.1)` and passes 757/757. Every place that writes
the version down moved with it — the default in `CMakeLists.txt` was the only
functional one, but hints and prose drift the moment one is missed:

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

Stage 6. **Done** (2026-09-06). `.pre-commit-config.yaml`'s
`mirrors-clang-format` rev moved from `v22.1.5` to `v23.1.0` — the newest the
mirror tags — so the formatter that polices the tree is on the same release
line as the one linked into the front end. `pre-commit run --all-files` is
clean: nothing reformatted, as measured below. Exactly the same line, not
the same build: the mirror publishes wheels per release, and 23.1.1 has none
yet. That gap is already the standing situation — the current `v22.1.5` rev
polices a tree built against 22.1.8 — and is why the *pin* is major.minor while
this rev is exact.

**This step is independent of every other one and costs nothing**: clang-format
22.1.5 and 23.1.0 both report the 136 tracked `.hpp`/`.cpp` files (vendor
excluded) already conformant, so nothing reformats.

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

## Questions

Decided and open questions share this namespace; **Status** says which is which,
and answering one graduates it in place so existing links stay valid.

### [constraints-fragment-spelling](#constraints-fragment-spelling)

**Question:** in derived *Constraints* wording, is the requires-expression
spelled `requires(const Impl& impl)` or `requires(const Impl & impl)` under
LLVM 23? **Status:** DECIDED, 2026-09-06. **Decided by:** Steve Downey.

**Decision:** `Impl&`. Restore it with a declaration-context wrapper; do not
regenerate the goldens to match clang-format 23.

**Why:** standardese requires it. `const T&` is the draft's spelling, not a
preference — which is why `PAS_Left` is set deliberately in the first place
(`frontend.cpp:436-437`: the reference binds to the type, not the declarator).
`Impl & impl` reads as an expression, which is exactly what clang-format 23
mistook it for; the tool's job is to emit wording a paper can paste, so a
formatter defect is not grounds to change what the wording says. The argument
against — permanent machinery for one release's parse — is real but is bounded
by what the fix turned out to be: nine lines, one call site, and a no-op on the
LLVM the tree is pinned to today.

**Log:**

- 2026-09-06 — decided; implemented as `format_expr_fragment` in
  [constraints-fragment-format](#constraints-fragment-format), which see for
  the shape and the measurements.
- Still worth reporting the bare-fragment case upstream. Not a blocker: the
  wrapper is correct regardless of whether clang-format changes back, since a
  declaration fragment formatted without a declaration context was always the
  tool asking clang-format to guess.

### [llvm-22-support-window](#llvm-22-support-window)

**Question:** after the move, does the tree still build against LLVM 22?
**Status:** DECIDED, 2026-09-06. **Decided by:** Steve Downey.

**Decision:** No window. Once [pin-bump](#pin-bump) lands, LLVM 23 *is* the
version — one pinned LLVM, exactly as decision
[llvm-toolchain-pin](../decisions/llvm-toolchain-pin.md) already reads.

**Why:** the barrier to entry is low. apt.llvm.org publishes a per-release
channel for every supported LLVM, so a contributor on a distro packaging an
older one adds a repository line and installs `llvm-23-dev` — this is how the
dev box got 23.1.1, and it is already the standing answer for anyone whose
distro ships 21. Against that, a support window buys little and costs a version
`#if` in `frontend.cpp` (the first in that file) permanently, on the very code
path — comment attachment — whose subtleties are hardest to keep straight
across two Clang versions at once. The pin exists so the front end is written
against one LLVM; carrying two would be a change to that decision rather than
an application of it, for a problem an apt line solves.

**Consequence for [pin-bump](#pin-bump):** the decision record is where this
belongs once the pin actually moves. Amend
[llvm-toolchain-pin](../decisions/llvm-toolchain-pin.md) to say the pin is a
floor as well as a ceiling — a single version, not a minimum — and to name
apt.llvm.org's per-release channels as the supported way to get one, so the
next reader is not left to infer it from the absence of an `#if`.

**Log:**

- 2026-09-06 — looked for a spelling both versions accept, which would make the
  window free. There is one that *compiles*:
  `ASTContext::getRawCommentForAnyRedecl` is public in both, and 23's
  `RawCommentLookupKey` overload takes a `const Decl*` by conversion. It is not
  a drop-in. Substituted for the NoCache call it builds clean on both and then
  fails four cases — the `build_document` unit test for documented namespace
  free-function definitions, and `golden.gathered_refs_generate` in all three
  modes — because the redecl-chain lookup finds comments the described-template
  lookup deliberately does not (the anchor rule the comment above
  `attached_raw_comment` sets out). So a window really does cost a version
  `#if`; there is no free spelling to hide behind.
