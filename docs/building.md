# Building and verifying

How to configure, build, test, and lint `beman.specgen`, and how to regenerate the
artifacts the tests compare against. Rationale for the toolchain choices lives under
`docs/decisions/`.

## Prerequisites

- **GCC 16.** The language floor is C++26 (see
  [decisions/cxx26-baseline.md](decisions/cxx26-baseline.md)); if the default `g++` is
  older, point the build at a newer one (the Makefile lane resolves
  `make TOOLCHAIN=<name>` to `cmake/<name>-toolchain.cmake`).
- **LLVM/Clang development packages.** The Clang front end is required:
  `find_package(Clang REQUIRED)` is unconditional, and there is no Clang-free
  configuration.
- **`uv`.** The project wraps CMake in `uv`, which also supplies the Python-side
  tooling (pre-commit and friends) from `pyproject.toml`.

## The LLVM version pin

`find_package(Clang)` is version-pinned by the cache variable
`BEMAN_SPECGEN_LLVM_VERSION` (default `22.1`). CMake's config search globs
`lib/cmake/clang*`, so an unversioned `find_package(Clang)` on a box with several
LLVMs installed side by side silently takes the newest — and a newer LLVM can rename
an API the front end calls, breaking the build the day it lands. Pass
`-DClang_DIR=<prefix>/lib/cmake/clang` for an LLVM off the default search path; a
`Clang_DIR` whose version does not match the request is **rejected** rather than
used, so moving to a new LLVM is one deliberate flag
(`-DBEMAN_SPECGEN_LLVM_VERSION=<version>`) and never an accident of what happens to
be installed. See [decisions/llvm-toolchain-pin.md](decisions/llvm-toolchain-pin.md).

## The preset lane

One preset, one configuration:

```sh
uv run cmake --preset gcc-release
uv run cmake --build --preset gcc-release
uv run ctest --preset gcc-release
```

That run reports **645 tests**. The count goes stale the moment a ctest case is
added; whoever adds one updates this number here in the same change.

The full preset list is `{gcc, llvm, appleclang, msvc}` × `{debug, release}`;
`gcc-release` is the one used day to day and the one CI reproduces. A box whose
`/usr/bin` carries a versioned `clang-<N>` but neither `clang++` nor `llvm-config`
needs a compiler hint before the `llvm-*` presets will configure.

A preset writes its build tree under `build/<preset>/` with
`CMAKE_INSTALL_PREFIX=/usr/local`, so installing from a preset build needs an
explicit `--prefix` and is not the documented install path (see below).

## The Makefile lane

The Makefile drives the same CMake project through Ninja Multi-Config (configuration
types `RelWithDebInfo;Debug;Tsan;Asan;Gcov`) into `.build/build-system` (or
`.build/build-<toolchain>` under `make TOOLCHAIN=<name>`; extra cache variables go
in `_cmake_args=`). It needs no `LD_LIBRARY_PATH` shim, because
`cmake/gcc-flags.cmake` embeds an rpath to the compiler's own libstdc++, and it
reports the same test count the preset lane does.

- `CONFIG` defaults to **`Asan`**, so a bare `make` or `make test` builds and tests
  under AddressSanitizer.
- `make release` builds `RelWithDebInfo` and `make install-release` installs it;
  `RELEASE_CONFIG` overrides the configuration. A sanitizer build is the wrong thing
  to install: the sanitizer writes to stderr, which corrupts byte-compared captured
  output, and a sanitized static library imposes Asan on every consumer that links
  it.
- `make install` places the driver at **`.install/bin/specgen`** (`PREFIX`
  selects another prefix), always as a RelWithDebInfo build regardless of the
  ambient `CONFIG`. That is the well-known location the examples and the live
  example document assume; every script honours a `$SPECGEN` override, so a preset
  build (`build/gcc-release/tools/specgen/specgen`) works without installing.
- The install components are `specgen_Runtime` and `specgen_Development`. Beware
  that `cmake --install` treats an unknown component as an empty one and exits 0, so
  an install that names a component no rule creates is silent.

## Lint

`make lint` runs everything pre-commit is configured with (clang-format, gersemi,
markdownlint, and the rest). Two gotchas:

- It **reformats and then fails on its first run** — that is the auto-fix landing.
  `git add -A` and run it again; the second result is the one that counts.
- Corpus headers (`tests/corpus/*.hpp`) are clang-format-controlled, and their
  formatting is part of the extracted output. If lint reformats one, **regenerate
  the generate-mode goldens** (`make goldens`) and re-verify.

## Coverage

`make coverage` builds the `Gcov` configuration and processes the results;
`make view-coverage` opens the HTML report. CI runs coverage as its own job and
publishes the report as an artifact; nothing uploads to an external service.

## Examples and documentation exports

- `make examples-md` and `make examples-html` export `docs/examples.org`;
  `make examples-live-html` exports the live document, executing its blocks against
  the installed binary (it takes `install-release` as an order-only prerequisite).
  All of these need Emacs (`EMACS` selects which one).
- Captured example output under `examples/cli/output/` is checked in and compared by
  `ctest -R examples.`. After changing anything a captured file depends on, re-run
  `examples/cli/run-all.sh` from a **non-sanitizer** build (`make install-release`,
  not the Asan default) and commit the result — the captures compare stderr byte for
  byte, and a sanitizer has standing permission to write there.

## End-to-end smoke test

The fastest way to eyeball a change:

```sh
specgen generate <header.hpp>
```

That is the single pass: front end and backends in one process, with the IR never
serialized. To see the IR itself, or to render on a machine without Clang, split it
in two:

```sh
specgen generate --emit-ir <header.hpp> | specgen render --from-ir -
```

Every generate-mode golden must also round-trip: `specgen render --from-ir
<expected.json>` exits 0. Each one additionally has a `.singlepass` sibling
requiring the one-command form above to print exactly what the two-command form
does. `make goldens` regenerates the golden files wholesale; a non-empty
`git diff --stat tests/golden/` afterwards is a finding to report, never an edit
to the golden.
