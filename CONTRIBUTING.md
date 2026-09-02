<!--
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
-->

# Contributing to beman.specgen

Start with [docs/building.md](docs/building.md), which holds the authoritative build, lint, and verification
commands, and [docs/CODING_RULES.md](docs/CODING_RULES.md), which decides whether a diff is acceptable. The
design reference is [docs/architecture.md](docs/architecture.md), and settled rationale lives in
[docs/decisions/](docs/decisions/).

## Requirements

- GCC 16 (libstdc++ with C++26 support)
- LLVM/Clang development packages matching the pinned version (`BEMAN_SPECGEN_LLVM_VERSION`, default `22.1`)
- CMake 3.30 or later and `uv` (the repository wraps CMake in `uv run`)
- Catch2 3 for the test suite (fetched automatically, or provided by vcpkg)

## Configure, build, and test with CMake presets

```bash
uv run cmake --preset gcc-release
uv run cmake --build --preset gcc-release
uv run ctest --preset gcc-release
```

The presets are `{gcc,llvm,appleclang,msvc}-{debug,release}`; each writes to `build/<preset>/`. The debug
presets configure the MaxSan sanitizer set. For an LLVM installed off the default search path, pass
`-DClang_DIR=<prefix>/lib/cmake/clang`; a `Clang_DIR` whose version does not match the pin is rejected
(see [docs/decisions/llvm-toolchain-pin.md](docs/decisions/llvm-toolchain-pin.md)).

## The Makefile workflow

`make` drives a Ninja Multi-Config tree with `RelWithDebInfo`, `Debug`, `Tsan`, `Asan`, and `Gcov`
configurations (default `CONFIG=Asan`). Useful targets: `make compile`, `make ctest`, `make lint`,
`make coverage`, `make release`, `make install-release` (installs `.install/bin/specgen`, which the
example scripts assume), and `make testinstall`. See [docs/building.md](docs/building.md) for the details
and gotchas — including that the first `make lint` run may reformat files and then fail; re-add and re-run.

## Dependency management

Catch2 is the only test dependency. The default path fetches it with CMake `FetchContent`, pinned by
`lockfile.json`. With vcpkg on `PATH`, the Makefile switches to the vcpkg toolchain and the custom triplet in
`cmake/x64-linux-custom.cmake` instead.

## Project-specific configure switches

| Option | Default | Effect |
| ------ | ------- | ------ |
| `BEMAN_SPECGEN_BUILD_TESTS` | `ON` | Build the Catch2 suites, golden cases, and example checks |
| `BEMAN_SPECGEN_BUILD_EXAMPLES` | `ON` | Build the compiled examples |
| `BEMAN_SPECGEN_BUILD_TOOLS` | `ON` | Build the `specgen` driver |
| `BEMAN_SPECGEN_USE_MODULES` | `OFF` | Also build the experimental C++ modules lane |
| `BEMAN_SPECGEN_LLVM_VERSION` | `22.1` | The LLVM/Clang version `find_package(Clang)` must match |

## Before you push

Run the full suite and the linters; both must be clean:

```bash
uv run ctest --preset gcc-release
make lint
```

A change to a corpus header (`tests/corpus/*.hpp`) usually moves golden files — regenerate them with the
build's driver rather than editing goldens by hand, and re-run `examples/cli/run-all.sh` from a
non-sanitizer build when a captured example output is affected.
