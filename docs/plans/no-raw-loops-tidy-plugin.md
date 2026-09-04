# Plan: the no-raw-loops gate as a clang-tidy plugin

**Status:** planned, not started. To be done together with pulling `clang-tidy` and its related gates into the
build. Nothing below is wired in yet; `tools/check-raw-loops.cmake` remains the live gate until the
[retire-text-gate](#retire-text-gate) step lands.

## Goal

Replace the line-oriented text scan behind `style.no-raw-loops` with an AST check that applies the *same* rule from
`docs/CODING_RULES.md` ("No raw loops"): every `for`, range-`for`, `while`, and `do` outside a substrate generic
algorithm is a defect unless it carries the exact marker `// substrate generic algorithm` plus a reason, on the loop
line or in the contiguous comment block directly above it. The marker, the doctrine, and the 109 existing marked sites
do not change. What changes is the mechanism: a clang-tidy check, loaded as a plugin into the pinned LLVM's own
`clang-tidy`, run as a distinct lint pass over every production source and every header as its own translation unit.

## Why a plugin and not stock clang-tidy

Measured against the current tree (LLVM 22.1 from the Debian packages, GCC 16 libstdc++, C++26):

- No built-in check bans loops; the loop-related checks rewrite loop forms.
- The query-based custom checks (`--experimental-custom-checks`, `CustomChecks:` in the config) flag every loop
  correctly, and matched the text gate site for site. But an AST matcher cannot see comments, so its only suppression
  is `NOLINT` on the loop line or `NOLINTNEXTLINE` on the line directly above. The doctrine's block form, a multi-line
  reason ending on the line above the loop, does not fit that shape. Adopting it means rewriting every marked site and
  changing what the marker is. The facility is also still labelled experimental.
- A plugin check has the AST *and* the raw source buffer. It matches the loop statements and then reads the marker
  rule exactly as the text gate reads it today. A prototype built this way, loaded with `--load`, reported zero
  findings over the tree and exactly five when five markers were stripped from a scratch copy of `docblock.cpp`.

So the plugin keeps the doctrine as written and rides clang-tidy's infrastructure (compile database, `run-clang-tidy`
parallelism, `--header-filter`, `--warnings-as-errors`, `NOLINT` for anything future) instead of re-implementing it.

## The `--load` mechanism

clang-tidy loads checks from a shared object named by `--load=<path>` (`run-clang-tidy` forwards it as `-load`). The
mechanism, and what it demands of the build:

**Registration.** The shared object defines a `ClangTidyModule` subclass whose `addCheckFactories` registers the
checks under `<module>-<check>` names, and a static registrar object:

```cpp
static clang::tidy::ClangTidyModuleRegistry::Add<SpecgenModule> X("specgen-module", "beman.specgen house checks");
```

The registrar's constructor runs at `dlopen` time and appends the module to `llvm::Registry<ClangTidyModule>`, the
same registry the built-in modules use. `ClangTidyModuleRegistry` is declared in `clang-tidy/ClangTidyModule.h`; the
separate `ClangTidyModuleRegistry.h` header is deprecated in LLVM 22 and removed in 24, so include only
`ClangTidyModule.h` and `ClangTidyCheck.h`.

**Symbol resolution.** The plugin is linked with undefined symbols left unresolved (the default for a shared library).
At load time they resolve against the `clang-tidy` executable and the shared `libclang-cpp` and `libLLVM` it links.
The Debian 22 binary exports what a plugin needs (the `ClangTidyCheck` vtable and members, the registry's
`Registry<ClangTidyModule>` instantiation); this was verified with `nm -D` and by loading the prototype. Two rules
follow:

- **Never link the plugin against `libclangTidy.a`.** That would give the plugin its own copy of the registry, and
  the check would register into a registry nobody reads. The plugin's only link inputs are its own objects.
- **The `clang-tidy` that loads the plugin must be the pinned LLVM's own binary**, found via
  `${LLVM_TOOLS_BINARY_DIR}` from the `find_package(Clang)` already done at the top level, never the one on `PATH`.
  A plugin built against one LLVM's headers and loaded into another's binary fails to load at best and crashes at
  worst. This is the same seam decision `llvm-toolchain-pin` already guards for the front end; the plugin is one
  more consumer of it.

**Compile flags.** The plugin is C++ compiled with the project's compiler (GCC) against LLVM's headers, exactly as
`beman.specgen.frontend` already is, so the same rules apply: take `${CLANG_INCLUDE_DIRS}` and `${LLVM_INCLUDE_DIRS}`
as `SYSTEM` includes, and mirror `LLVM_ENABLE_RTTI` (`-fno-rtti` only when the LLVM was built without RTTI; the
Debian 22 build has it on). The prototype compiled with `-std=c++20`; the target should ask for `cxx_std_26` like
the rest of the tree and fall back only if LLVM's headers object. Build it as a CMake `MODULE` library, not `SHARED`:
nothing links against it, it is only ever `dlopen`ed.

**Headers.** The clang-tidy headers live beside Clang's (`<prefix>/include/clang-tidy/`) on Debian, but they come
from clang-tools-extra and some distributions package them separately. Probe for `clang-tidy/ClangTidyCheck.h` under
`${CLANG_INCLUDE_DIRS}` at configure time and make the plugin target, and the gate that uses it, conditional on the
probe succeeding, with a `STATUS` message either way. The text gate needs nothing built, so its replacement must say
clearly when it is not available rather than silently passing.

## The check

Name: `specgen-no-raw-loops`, in module `specgen-module`. Semantics, restated for the AST so they stay identical to
`tools/check-raw-loops.cmake`:

- Match `forStmt`, `cxxForRangeStmt`, `whileStmt`, `doStmt`, `unless(isInTemplateInstantiation())` so a loop in a
  template body is one site, not one per instantiation.
- The site's location is the expansion location of the statement's begin, so a loop produced by a macro is reported
  at the macro's use. The text gate could not see those at all.
- A site is marked if the marker text appears on the site's own line of the file buffer (trailing-comment form, as
  `foundation/monoid.hpp` writes it) or in the contiguous run of comment lines directly above it (block form, as
  `backend/common.hpp` writes it), walking upward while each line's first non-blank characters are `//`, `/*`, or
  `*`, and stopping at the first line that is not. A marker separated from its loop by code marks nothing.
- Diagnostic text: `raw loop outside a substrate generic algorithm; convert it to a named algorithm, or mark it
  '// substrate generic algorithm: <reason>' on the loop line or in the comment block directly above it`.

Of the text gate's recorded limits, the AST removes two (a `for` whose `(` is on the next line; a loop inside a macro)
and keeps one (the marker matched as raw text is found in a string literal too). The `} while (...)` second-half
special case disappears, since a `DoStmt` is one node.

## The pass

The gate is a distinct lint pass, not a co-compile: `CMAKE_CXX_CLANG_TIDY` stays unset. Co-compiling would put a
parse of every TU on the build's critical path for a check whose answer does not change between configurations.

**Inventory.** Configure with `CMAKE_VERIFY_INTERFACE_HEADER_SETS=ON` in the lint preset. CMake then generates one
translation unit per header in each target's `FILE_SET HEADERS` (26 today across the core and the front end), and
`CMAKE_EXPORT_COMPILE_COMMANDS` puts them in the compile database. That is the machinery for "every header as its
own TU"; nothing bespoke is needed, and with it on, the AST inventory matched the text gate's inventory exactly, the
two loops in `foundation/traverse.hpp` included (no production TU includes that header). The pass takes the compile
database and filters it with the text gate's exclusion list: `tests/`, `vendor/`, `*.test.cpp`, plus `_deps/`, since
the fetched Catch2 sources appear in the database once tests are configured. Whether `examples/` joins the scope is
an open question below.

**Driver.** `run-clang-tidy` from the pinned LLVM, with `-p <build>`, `-load <plugin>`, `-checks='-*,specgen-*'`,
`-warnings-as-errors='specgen-*'`, `-header-filter` covering `include/`, `src/`, and `tools/` under the repo, and
the filtered file list. A thin script under `tools/` owns the filtering and the argument list so the ctest case and a
developer at the prompt run the same command. Exit status is the gate.

**Cost.** Measured on the prototype, eight-way parallel over the 38 production and header TUs: 45 s wall. The floor
is `frontend.cpp` alone at about 40 s and 1.8 GB, because it includes Clang's headers; the 26 header TUs are well
under a second each. The text gate runs in 3 s with nothing built. That is the price, and it is paid once per lint
run, not per build.

## Testing the check

The check gets its own tests, independent of the tree it guards. A directory of small probe sources, one per rule
edge, each run through the pinned `clang-tidy` with the plugin and compared against an expected-diagnostics file:

- marked, trailing-comment form; marked, block form spanning several lines;
- unmarked `for`, range-`for`, `while`, `do`;
- a marker above the enclosing function with three loops inside (all three must be reported);
- a marker separated from its loop by one line of code (reported);
- a `for` with its parenthesis on the following line (reported; the text gate missed this);
- a loop expanded from a macro (reported at the use);
- a loop in a template used twice (reported once).

Plus the negative test already run by hand: strip the markers from a copy of a production file and require exactly
that many findings.

## Steps

Each is one PR-sized change; the stage number is reading order only. Cross-reference by the slug, never the
number.

### tidy-toolchain-probe

Stage 1. Find the pinned LLVM's `clang-tidy` and `run-clang-tidy` via `${LLVM_TOOLS_BINARY_DIR}`, and probe for
`clang-tidy/ClangTidyCheck.h` under `${CLANG_INCLUDE_DIRS}`. Record the results as cache variables and `STATUS` lines.
Amend decision `llvm-toolchain-pin` to say the pin covers clang-tidy and any plugin built against it.

### tidy-plugin-target

Stage 2. Add the `MODULE` library target under `tools/` (suggested `tools/tidy/`), conditional on the probe, with the
`SYSTEM` includes and RTTI handling copied from `src/beman/specgen/frontend/CMakeLists.txt`. Module registration only
at this stage; `--list-checks` with `--load` proves the load.

### no-raw-loops-check

Stage 3. Implement `specgen-no-raw-loops` as specified above, with the probe-file tests. Green on the whole tree with
zero findings, and the strip-the-markers negative test passing.

### header-tu-inventory

Stage 4. Turn on `CMAKE_VERIFY_INTERFACE_HEADER_SETS` in the preset the lint runs from, and write the filtering script
that turns the compile database into the pass's file list. Assert, once, that the resulting site inventory equals the
text gate's (109 today).

### ctest-gate

Stage 5. Register the pass as `style.no-raw-loops`, replacing the command of the existing case, conditional on the
probe. When the probe fails, register a case that fails with a message naming what is missing, so the gate's absence is
visible in `ctest` output rather than a silent green.

### retire-text-gate

Stage 6. Delete `tools/check-raw-loops.cmake` and update `docs/CODING_RULES.md` ("No raw loops"), `docs/building.md`,
and `AGENTS.md` to name the check and the lint preset. Two gates for one rule drift; the doctrine keeps one.

## Related gates to ride the same pass

Once the pass exists, other clang-tidy checks join it by editing the `-checks` list, at no new machinery. First
candidates, from a trial run on the current tree:

- `misc-include-cleaner`: zero findings in the 25 headers (every header already includes its direct dependencies),
  183 in the production `.cpp` files, nearly all symbols reached through a transitive include. That is a mechanical
  clean-up PR before the check can be a gate.
- `misc-header-include-cycle`: no findings expected; cheap insurance.

## Open questions

### [examples-scope](#examples-scope)

**Question:** does `examples/` fall under the no-raw-loops doctrine? **Status:** OPEN. The text gate excludes it; the
AST inventory found one unmarked loop at `examples/emit_ir.cpp:19`. Either mark it and add `examples/` to the scope,
or record the exclusion in `docs/CODING_RULES.md` so it reads as a decision rather than an accident.

### [text-gate-fast-path](#text-gate-fast-path)

**Question:** keep the text scan as a no-build fast path beside the plugin? **Status:** OPEN, leaning no. It answers
in 3 s with nothing configured, which the plugin cannot; but two implementations of one rule drift, and the plugin's
answer is the more precise one. The [retire-text-gate](#retire-text-gate) step assumes no.
