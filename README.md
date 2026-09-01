# beman.tree_algorithms: Algorithms for Trees

<!--
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
-->

<!-- markdownlint-disable line-length -->
[![Library Status](https://raw.githubusercontent.com/bemanproject/beman/refs/heads/main/images/badges/beman_badge-beman_library_under_development.svg)](https://github.com/bemanproject/beman/blob/main/docs/beman_library_maturity_model.md#the-beman-library-maturity-model)
[![Continuous Integration Tests](https://github.com/bemanproject/tree_algorithms/actions/workflows/ci_tests.yml/badge.svg)](https://github.com/bemanproject/tree_algorithms/actions/workflows/ci_tests.yml)
[![Lint Check (pre-commit)](https://github.com/bemanproject/tree_algorithms/actions/workflows/pre-commit-check.yml/badge.svg)](https://github.com/bemanproject/tree_algorithms/actions/workflows/pre-commit-check.yml)
[![Coverage](https://coveralls.io/repos/github/bemanproject/tree_algorithms/badge.svg?branch=main)](https://coveralls.io/github/bemanproject/tree_algorithms?branch=main)
![Standard Target](https://github.com/bemanproject/beman/blob/main/images/badges/cpp29.svg)
<!-- markdownlint-restore -->

`beman.tree_algorithms` is a family of generic algorithms over recursive tree
structures. The structural verbs `fold_fix`, `unfold_fix`, and `refold` consume,
build, and fuse fixed-point trees; `fold_with` and `unfold_with` are the same
folds over a tree in its own representation, through an explicitly supplied
projection or embedding — no conversion, no fixed-point wrapper materialized;
and `fold_map` is the elementwise fold, derived from the structural verbs. The
user supplies a non-recursive per-layer operation; the algorithm supplies the
recursion. No tree container is proposed — the algorithms work across
representations users already have.

**Implements**: the algorithm family proposed in [Algorithms for Trees
(D4322R0)](papers/algorithms-for-trees.md).

**Status**: [Under development and not yet ready for production use.](https://github.com/bemanproject/beman/blob/main/docs/beman_library_maturity_model.md#under-development-and-not-yet-ready-for-production-use)

## License

`beman.tree_algorithms` is licensed under the Apache License v2.0 with LLVM Exceptions.

## Usage

Describe one layer of your tree as a base functor — a struct with a type
parameter in the recursive positions — and each algorithm becomes a call with a
non-recursive per-layer operation:

```c++
// Evaluate an expression tree: the algebra sees children that are
// already ints; fold_fix supplies the recursion.
int result = fold_fix<int>(eval_algebra, tree);

// Sum the constants instead: map each element, combine associatively.
int sum = fold_map<int>(std::identity{}, std::plus{}, 0, tree);
```

Full runnable examples can be found in [`examples/`](examples/):

* [`fixpoint_tree_example.cpp`](examples/fixpoint_tree_example.cpp) — a
  self-contained expression tree and fold, suitable for Compiler Explorer.
* [`expression_algorithms.cpp`](examples/expression_algorithms.cpp) — algebras
  as reusable algorithm objects over the repository's expression tree.
* [`binary_tree_adapt.cpp`](examples/binary_tree_adapt.cpp) — adapting an
  existing `shared_ptr` binary tree by conversion (`to_fix`/`from_fix`).
* [`nonce_tree_direct.cpp`](examples/nonce_tree_direct.cpp) — folding and
  building a move-only `unique_ptr` tree in place with `fold_with` and
  `unfold_with`; no conversion, no fixed-point wrapper.
* [`search_tree_on_fix.cpp`](examples/search_tree_on_fix.cpp) — a full working
  binary search tree built on `Fix` directly: folds where they fit, plain
  structural recursion where they don't.
* [`sequence_algorithms.cpp`](examples/sequence_algorithms.cpp) — sequence
  algorithms written against a quotient interface that never sees tree shape.

## Dependencies

### Build Environment

This project requires at least the following to build:

* A C++ compiler that conforms to the C++23 standard or greater
* CMake 3.30 or later
* (Test Only) Catch2

You can disable building tests by setting CMake option `BEMAN_TREE_ALGORITHMS_BUILD_TESTS` to
`OFF` when configuring the project.

### Supported Platforms

The library requires C++23 with deducing-this (explicit object parameters), so the toolchain floor is GCC 14 and Clang 18.

| Compiler   | Version | C++ Standards | Standard Library  |
|------------|---------|---------------|-------------------|
| GCC        | 16-14   | C++23         | libstdc++         |
| Clang      | 22-18   | C++23         | libstdc++, libc++ |
| AppleClang | latest  | C++23         | libc++            |
| MSVC       | latest  | C++23         | MSVC STL          |

C++26 is additionally exercised by an advisory (allowed-to-fail) CI job on the newest GCC.

## Development

See the [Contributing Guidelines](CONTRIBUTING.md).

## Integrate beman.tree_algorithms into your project

### Build

You can build tree_algorithms using a CMake workflow preset:

```bash
cmake --workflow --preset gcc-release
```

To list available workflow presets, you can invoke:

```bash
cmake --list-presets=workflow
```

For details on building beman.tree_algorithms without using a CMake preset, refer to the
[Contributing Guidelines](CONTRIBUTING.md).

### Installation

#### Vcpkg

The preferred way to install tree_algorithms is via vcpkg. To do so, after installing vcpkg
itself, you need to add support for the Beman project's [vcpkg
registry](https://github.com/bemanproject/vcpkg-registry) by configuring a
`vcpkg-configuration.json` file (which tree_algorithms [provides](vcpkg-configuration.json)).

Then, simply run `vcpkg install beman-tree-algorithms`.

#### Manual

To install beman.tree_algorithms globally after building with the `gcc-release` preset, you can
run:

```bash
sudo cmake --install build/gcc-release
```

Alternatively, to install to a prefix, for example `/opt/beman`, you can run:

```bash
sudo cmake --install build/gcc-release --prefix /opt/beman
```

This will generate the following directory structure:

```txt
/opt/beman
├── include
│   └── beman
│       └── tree_algorithms
│           ├── tree_algorithms.hpp
│           └── ...
└── lib
    └── cmake
        └── beman.tree_algorithms
            ├── beman.tree_algorithms-config-version.cmake
            ├── beman.tree_algorithms-config.cmake
            └── beman.tree_algorithms-targets.cmake
```

### CMake Configuration

If you installed beman.tree_algorithms to a prefix, you can specify that prefix to your CMake
project using `CMAKE_PREFIX_PATH`; for example, `-DCMAKE_PREFIX_PATH=/opt/beman`.

You need to bring in the `beman.tree_algorithms` package to define the `beman::tree_algorithms` CMake
target:

```cmake
find_package(beman.tree_algorithms REQUIRED)
```

You will then need to add `beman::tree_algorithms` to the link libraries of any libraries or
executables that include `beman.tree_algorithms` headers.

```cmake
target_link_libraries(yourlib PUBLIC beman::tree_algorithms)
```

### Using beman.tree_algorithms

To use `beman.tree_algorithms` in your C++ project,
include an appropriate `beman.tree_algorithms` header from your source code.

```c++
#include <beman/tree_algorithms/tree_algorithms.hpp>
```
