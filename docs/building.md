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
`BEMAN_SPECGEN_LLVM_VERSION` (default `23.1`). CMake's config search globs
`lib/cmake/clang*`, so an unversioned `find_package(Clang)` on a box with several
LLVMs installed side by side silently takes the newest — and a newer LLVM can rename
an API the front end calls, breaking the build the day it lands. Pass
`-DClang_DIR=<prefix>/lib/cmake/clang` for an LLVM off the default search path; a
`Clang_DIR` whose version does not match the request is **rejected** rather than
used, so moving to a new LLVM is one deliberate flag
(`-DBEMAN_SPECGEN_LLVM_VERSION=<version>`) and never an accident of what happens to
be installed. The pin is per build tree: it is a cache variable, so a tree
configured before the default moved keeps the old value and the old `find_*`
answers with it. After the pin moves, start a fresh build directory rather than
reconfiguring the old one (the Makefile refuses to, see below).
See [decisions/llvm-toolchain-pin.md](decisions/llvm-toolchain-pin.md).

### Linking Clang

`BEMAN_SPECGEN_LLVM_LINK` picks how the front end links Clang: `shared` (the
distribution's `clang-cpp` and `LLVM`), `static` (the Clang component archives,
with `--gc-sections`), or `auto` (the default: `shared` when the LLVM has a
`clang-cpp`, `static` when it does not). How LLVM itself is linked follows from
the Clang package rather than from this option: Debian's static Clang archives
name the shared `libLLVM`, so `static` against apt.llvm.org's LLVM still loads
`libLLVM.so`, while against the official release tarball (or an LLVM built
without `LLVM_LINK_LLVM_DYLIB`) the driver loads no LLVM library at all. The
second is the portable release binary
([plans/manylinux-release-binary.md](plans/manylinux-release-binary.md)).

```sh
uv run cmake --preset gcc-release -B build/static-tarball \
  -DClang_DIR=<LLVM-23.1.x-Linux-X64>/lib/cmake/clang -DBEMAN_SPECGEN_LLVM_LINK=static
```

Without `clang-tidy` beside that LLVM, `style.no-raw-loops` fails rather than
passing silently; that is the gate working, not the build breaking.

## The preset lane

One preset, one configuration:

```sh
uv run cmake --preset gcc-release
uv run cmake --build --preset gcc-release
uv run ctest --preset gcc-release
```

That run reports **989 tests**. The count goes stale the moment a ctest case is
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
- `make ctest` runs the tests without building, but it regenerates first
  (`make regenerate`, a no-op when nothing is stale), so a test whose command line
  moved in a `CMakeLists.txt` runs as now registered rather than as last generated.
  It still runs against whatever was last built; `make test` builds first.
- A build tree is configured against one LLVM and **cannot change it in place**:
  `find_package` and `find_program` keep their cached answers, so reconfiguring
  with a new `Clang_DIR` would mix two toolchains. Every building or testing
  target first checks the tree's cached `Clang_DIR` against the `CLANG_DIR` the
  Makefile would pass, and stops naming the fix when they differ: `make realclean`
  (with the same `TOOLCHAIN=`, if any), then build again.
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
markdownlint, and the rest).

The "No raw loops" doctrine (`docs/CODING_RULES.md`) is enforced separately, as
the ctest case `style.no-raw-loops`: the `specgen-no-raw-loops` clang-tidy
plugin check run by the pinned `run-clang-tidy` over the compile database —
every production TU plus every `FILE_SET` header as its own verification TU,
which is why `CMAKE_VERIFY_INTERFACE_HEADER_SETS` is on in the `gcc-release`
preset, the one the lint runs from. It needs the pinned LLVM's `clang-tidy`
and the clang-tools-extra headers; configure's
`beman.specgen: clang-tidy plugin toolchain` STATUS line says whether the
probe found them, and without them the case fails naming the missing piece
rather than going silently green. The pass re-parses with clang against a
scrubbed copy of the compile database: instrumentation flags, `-Werror`, and
the GCC module-scanning flags are removed, and a multi-config database (the
Makefile lane) is reduced to one entry per file, so the gate gives one answer
per lane and parses each TU once. Budget ~45 s per run; `ctest -R style`
selects it, and `ctest -R tidy.` runs the check's own probe battery.

Two pre-commit gotchas:

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

## Release binary

Every push to `main` publishes a GitHub Release carrying a portable driver,
`specgen-manylinux_2_34_x86_64.tar.gz`, built by
`.github/workflows/release-binary.yml`
([decisions/portable-release-binary.md](decisions/portable-release-binary.md);
the plan and its measurements are
[plans/manylinux-release-binary.md](plans/manylinux-release-binary.md)). Pull
requests that touch the release files run everything but the publishing.

It is built inside the `specgen-toolchain` image, whose two layers
`packaging/release/{base,llvm}.Dockerfile` describe and
`.github/workflows/toolchain-image.yml` publishes to
`ghcr.io/steve-downey/specgen-toolchain`: AlmaLinux 9 with glibc 2.34, GCC 16
from `gcc-toolset-16`, and the official LLVM 23.1 release's static archives in
`/opt/llvm`. The release workflow takes the image by the digest in
`packaging/release/images.env`; after a change to the Dockerfiles, copy the new
`llvm` digest from the toolchain workflow's summary into that file.

The `manylinux-release` preset is the build, and only works inside the image:
`Clang_DIR=/opt/llvm/lib/cmake/clang`, `BEMAN_SPECGEN_LLVM_LINK=static`, and
the image's own `cc`/`c++`. Scripts in `tools/release/` do the rest, and the
workflow runs them the same way a local build does:

- `stage.sh <build> <out>` installs the `specgen_Runtime` component, adds
  `LICENSE` and the archive's README (`packaging/release/README.md`), splits
  out and compresses the debug information, strips the driver, and writes the
  archive and its checksum.
- `check-portable.sh <staged dir> [<commit> [<clang argument>...]]` fails
  unless the driver needs only libraries `manylinux_2_34` allows, references no
  symbol version newer than `GLIBC_2.34`, `GLIBCXX_3.4.29` and `CXXABI_1.3.13`,
  needs no instruction set beyond x86-64-v2, and passes `smoke.sh`.
- `smoke.sh <specgen> [<commit> [<clang argument>...]]` runs the driver from an
  empty directory: `--version`, `generate` on `tools/release/smoke/smoke.hpp`
  (which includes the host's `<cstddef>` and `<optional>`), and the
  `--emit-ir` / `render --from-ir` round trip.
- `build-wheel.sh <staged dir> <out>` packages the staged tree as the
  `beman-specgen` wheel (`packaging/pypi/`, built with scikit-build-core; it
  compiles nothing) and has `auditwheel repair` tag it `manylinux_2_34_x86_64`,
  failing if auditwheel will not.
- `check-wheel.sh <wheel> [<commit> [<clang argument>...]]` installs the wheel
  into a fresh virtual environment and runs `smoke.sh` on its `specgen`
  command.

To reproduce the release build locally, mount the checkout read-only and keep
the build and the virtual environment out of it:

```sh
image=$(sed -n 's/^SPECGEN_TOOLCHAIN_IMAGE=//p' packaging/release/images.env)
docker run --rm -v "$PWD":/src:ro -e UV_PROJECT_ENVIRONMENT=/tmp/venv -w /src "$image" sh -c '
  uv run cmake --preset manylinux-release -B /tmp/build &&
  uv run cmake --build /tmp/build &&
  uv run ctest --test-dir /tmp/build -E "^style\." &&
  uv run tools/release/stage.sh /tmp/build /tmp/dist &&
  tools/release/check-portable.sh /tmp/dist/specgen-*-manylinux_2_34_x86_64 "" \
    --gcc-install-dir="$(dirname "$(g++ -print-libgcc-file-name)")"'
```

The `style.` tests are excluded because the image has no `clang-tidy`; lint is
a pull-request check. `--gcc-install-dir` is there because the image's only
C++ standard library is gcc-toolset's, under `/opt/rh`, where Clang's GCC
detection does not look. Mounting a git worktree rather than a clone also needs
its git directory mounted at the path the worktree's `.git` file names, or
`--version` reports `git unknown` and the commit check fails, which is why the
command above passes an empty commit.

auditwheel's instruction-set check is the one part of the policy
`build-wheel.sh` turns off. Everything linked on EL9 needs x86-64-v2 (its
compilers default to `-march=x86-64-v2`, and its `crt1.o` carries the marking),
while a `manylinux_2_34_x86_64` tag means baseline x86-64 and has no way to say
v2. The requirement is documented instead, and `check-portable.sh` fails if
anything ever needs more than v2.

### Publishing the wheel

The release workflow builds and checks the wheel on every run and uploads it
as the `beman-specgen-wheel` artifact. It publishes only when the repository
variable `PYPI_PUBLISH` is `testpypi` or `pypi`, from a push to `main`, and
only a `PROJECT_VERSION` that index does not already have: versions on an index
are immutable, so bumping the CMake version is what publishes. It uses PyPI's
trusted publishing, so no token is stored. To turn it on for an index:

1. On the index (`test.pypi.org` or `pypi.org`), under *Your projects →
   Publishing*, add a pending publisher: project `beman-specgen`, owner
   `steve-downey`, repository `specgen`, workflow `release-binary.yml`, and
   environment `testpypi` or `pypi` to match.
2. In the repository's *Settings → Secrets and variables → Actions →
   Variables*, set `PYPI_PUBLISH` to that environment's name. The workflow
   creates the environment on first use; add protection rules to it there if
   publishing should wait for an approval.
3. Push to `main`, or open the latest `main` run of the release workflow and
   re-run only its *Publish the wheel* job. Do not re-run all jobs: *Publish
   the release* would create the run's tag a second time, and fail. The
   single job reads `PYPI_PUBLISH` afresh and reuses the run's wheel
   artifact.

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

Each generate-mode golden also has a `.quiet` sibling requiring the run to report
**nothing** on stderr: the corpus is meant to be well-formed markup, and only a
`MODE diagnose` case ever looks at findings, so without this a typo in an ordinary
header warned on every run and nothing noticed. There is no opt-out — a header
whose finding is the point gets a `MODE diagnose` case, which pins the exact text.
