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
It lowers source declarations and attached specgen markup to a semantic IR, then renders draft
LaTeX, mpark/wg21 markdown, or org fragments.

**Status**: [Under development and not yet ready for production use.](https://github.com/bemanproject/beman/blob/main/docs/beman_library_maturity_model.md#under-development-and-not-yet-ready-for-production-use)

## Documentation

- [User guide](docs/user-guide.md): markup, commands, diagnostics, and examples
- [Examples](docs/examples.md): transcluded headers and rendered outputs
- [Architecture and wording model](docs/architecture.md): the design reference source comments cite as `design §N`
- [Building and verification](docs/building.md): presets, the Makefile workflow, lint, and the test suite
- [Decision records](docs/decisions/README.md): the project's settled architectural decisions
- [Acid test](docs/acid-test.md): running specgen against the real `beman.optional` header out of tree

## Quick Start

The build requires the pinned LLVM/Clang development packages (see [Requirements](#requirements)):

```bash
uv run cmake --preset gcc-release
uv run cmake --build --preset gcc-release
uv run ctest --preset gcc-release
```

Generate and render a document through the semantic IR:

```bash
build/gcc-release/tools/specgen/specgen generate --emit-ir header.hpp -- \
  -std=c++2c > wording.json
build/gcc-release/tools/specgen/specgen render --from-ir wording.json \
  --backend latex > wording.tex
```

Use `--backend mpark` or `--backend org` for the other serializers.
`render --split <directory>` writes one fragment per top-level stable-name section.
See the [user guide](docs/user-guide.md) for compiler arguments, compilation database discovery,
validation, and the complete marker vocabulary.

A very small in-tree example uses the widget corpus header:

```bash
SPECGEN=build/gcc-release/tools/specgen/specgen
$SPECGEN generate --emit-ir tests/corpus/spec_widget.hpp -- \
  -std=c++2c > widget.json
$SPECGEN render --from-ir widget.json --backend latex -o widget.tex
```

The fuller [examples document](docs/examples.md) shows the corresponding headers, draft LaTeX,
mpark/wg21 markdown, org output, validation diagnostics, fragment splitting, paper mode,
compilation database input, and `dump-decls`.
Its commands are the scripts in [`examples/cli/`](examples/cli), and its output is what those scripts printed.
`examples/cli/run-all.sh` captures it; `ctest -R examples.` checks every captured file, most against the golden suite.

## Requirements

- GCC 16 with libstdc++ and C++26 support
- CMake 3.30 or later
- `uv` for the repository's CMake wrappers
- LLVM and Clang development packages 22 or later for header generation
- Catch2 3 when building tests

The supported Clang front-end configurations use Clang 22 or 23 with GCC 16's libstdc++.
The LLVM version is pinned by `BEMAN_SPECGEN_LLVM_VERSION` (default `22.1`); see [docs/building.md](docs/building.md).
Set `BEMAN_SPECGEN_BUILD_TESTS=OFF` to omit the test suite.

## Running

The preset build writes the executable to `build/gcc-release/tools/specgen/specgen`.
`make install` installs it as `.install/bin/specgen`, the location the examples assume.
`INSTALL_PREFIX` selects another prefix.
Installing from the preset tree needs an explicit `--prefix`; that tree is configured with `/usr/local`.
Invoke either binary directly or add its directory to `PATH`.

## License

`beman.specgen` is licensed under the Apache License v2.0 with LLVM Exceptions.

## Development

See the [contributing guidelines](CONTRIBUTING.md) and [docs/building.md](docs/building.md) for
the build matrix and verification commands.
