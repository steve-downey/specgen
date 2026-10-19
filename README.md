# beman.specgen: Standardese Generator

<!--
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
-->

<!-- markdownlint-disable line-length -->
[![Library Status](https://raw.githubusercontent.com/bemanproject/beman/refs/heads/main/images/badges/beman_badge-beman_library_under_development.svg)](https://github.com/bemanproject/beman/blob/main/docs/beman_library_maturity_model.md#the-beman-library-maturity-model)
[![Continuous Integration Tests](https://github.com/steve-downey/specgen/actions/workflows/ci_tests.yml/badge.svg)](https://github.com/steve-downey/specgen/actions/workflows/ci_tests.yml)
[![Lint Check (pre-commit)](https://github.com/steve-downey/specgen/actions/workflows/pre-commit-check.yml/badge.svg)](https://github.com/steve-downey/specgen/actions/workflows/pre-commit-check.yml)
![Standard Target](https://github.com/bemanproject/beman/blob/main/images/badges/cpp26.svg)
<!-- markdownlint-restore -->

`beman.specgen` is a Clang-based command-line tool that generates C++ standard-library description
wording from structured C++26 headers.
It reads a header's declarations and the specgen markup attached to them, and renders the wording as
draft LaTeX, mpark/wg21 markdown, or org.

**Status**: [Under development and not yet ready for production use.](https://github.com/bemanproject/beman/blob/main/docs/beman_library_maturity_model.md#under-development-and-not-yet-ready-for-production-use)

## Documentation

- [User guide](docs/user-guide.md): markup, commands, diagnostics, and examples
- [Examples](docs/examples.md): transcluded headers and rendered outputs
- [Architecture and wording model](docs/architecture.md): the design reference source comments cite as `design §N`
- [Building and verification](docs/building.md): presets, the Makefile workflow, lint, and the test suite
- [Decision records](docs/decisions/README.md): the project's settled architectural decisions
- [Acid test](docs/acid-test.md): running specgen against the real `beman.optional` header out of tree

## Install a prebuilt binary

Every push to `main` publishes a [GitHub Release](https://github.com/steve-downey/specgen/releases) with a
prebuilt `specgen` for x86_64 Linux. It runs on glibc 2.34 or newer (Ubuntu 22.04, Debian 12, Fedora 35,
RHEL 9 and their derivatives, or later) on an x86-64-v2 processor, and needs neither GCC 16 nor LLVM:

```bash
curl -sSL https://github.com/steve-downey/specgen/releases/latest/download/specgen-manylinux_2_34_x86_64.tar.xz | tar -xJ
specgen-*-manylinux_2_34_x86_64/bin/specgen --version
```

`tar -xJ` needs `xz` (`xz-utils` on Debian and Ubuntu); the same tree is also published as
`specgen-manylinux_2_34_x86_64.tar.gz` for `tar -xz`.

Keep the unpacked directory together; `lib/clang/` beside `bin/` holds Clang's builtin headers.
`specgen` does not carry a C++ standard library: a header that includes one is parsed against the host's,
so install `g++` (see the [user guide](docs/user-guide.md#what-specgen-needs-when-it-runs)).
Each release also carries `specgen.debug.xz`, which resolves the offsets in a crash report.

## Quick Start

Building from source requires the pinned LLVM/Clang development packages (see [Requirements](#requirements)):

```bash
uv run cmake --preset gcc-release
uv run cmake --build --preset gcc-release
uv run ctest --preset gcc-release
```

Generate the wording for a header:

```bash
build/gcc-release/tools/specgen/specgen generate header.hpp --validate \
  --backend latex -o wording.tex -- -std=c++2c
```

`--backend mpark` and `--backend org` select the other two output formats, and
`--split <directory>` writes one fragment per top-level stable-name section.
The [user guide](docs/user-guide.md) covers compiler arguments, compilation database discovery,
validation, and the complete marker vocabulary.

A very small in-tree example uses the widget corpus header:

```bash
SPECGEN=build/gcc-release/tools/specgen/specgen
$SPECGEN generate tests/corpus/spec_widget.hpp --no-compile-commands \
  --backend latex -o widget.tex
```

The fuller [examples document](docs/examples.md) shows the corresponding headers, draft LaTeX,
mpark/wg21 markdown, org output, validation diagnostics, fragment splitting, paper mode,
compilation database input, and `dump-decls`.
Its commands are the scripts in [`examples/cli/`](examples/cli), and its output is what those scripts printed.
`examples/cli/run-all.sh` captures it; `ctest -R examples.` checks every captured file, most against the golden suite.

## Requirements

Building from source needs the following; the [prebuilt binary](#install-a-prebuilt-binary) needs only glibc 2.34
and the host's `g++`.

- GCC 16 with libstdc++ and C++26 support
- CMake 3.30 or later
- `uv` for the repository's CMake wrappers
- LLVM and Clang 23.1 development packages for the required front end
- Catch2 3 when building tests

The source compiler may be GCC 16 or Clang 22–23 with GCC 16's libstdc++; the Clang tooling
libraries are independently pinned to the exact version named by `BEMAN_SPECGEN_LLVM_VERSION`
(default `23.1`). See [docs/building.md](docs/building.md).
Set `BEMAN_SPECGEN_BUILD_TESTS=OFF` to omit the test suite.

## Running

The preset build writes the executable to `build/gcc-release/tools/specgen/specgen`.
`make install` installs it as `.install/bin/specgen`, the location the examples assume.
`PREFIX` selects another prefix.
Installing from the preset tree needs an explicit `--prefix`; that tree is configured with `/usr/local`.
Invoke either binary directly or add its directory to `PATH`.

## License

`beman.specgen` is licensed under the Apache License v2.0 with LLVM Exceptions.

## Development

See the [contributing guidelines](CONTRIBUTING.md) and [docs/building.md](docs/building.md) for
the build matrix and verification commands.
