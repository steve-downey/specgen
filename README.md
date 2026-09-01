# beman.specgen: Spec Generator

<!--
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
-->

<!-- markdownlint-disable line-length -->
[![Library Status](https://raw.githubusercontent.com/bemanproject/beman/refs/heads/main/images/badges/beman_badge-beman_library_under_development.svg)](https://github.com/bemanproject/beman/blob/main/docs/beman_library_maturity_model.md#the-beman-library-maturity-model)
[![Continuous Integration Tests](https://github.com/bemanproject/specgen/actions/workflows/ci_tests.yml/badge.svg)](https://github.com/bemanproject/specgen/actions/workflows/ci_tests.yml)
[![Lint Check (pre-commit)](https://github.com/bemanproject/specgen/actions/workflows/pre-commit-check.yml/badge.svg)](https://github.com/bemanproject/specgen/actions/workflows/pre-commit-check.yml)
[![Coverage](https://coveralls.io/repos/github/bemanproject/specgen/badge.svg?branch=main)](https://coveralls.io/github/bemanproject/specgen?branch=main)
![Standard Target](https://github.com/bemanproject/beman/blob/main/images/badges/cpp29.svg)
<!-- markdownlint-restore -->

`beman.specgen` is (... TODO: description).

**Implements**: `std::todo` proposed in [TODO (PnnnnRr)](https://wg21.link/PnnnnRr).

**Status**: [Under development and not yet ready for production use.](https://github.com/bemanproject/beman/blob/main/docs/beman_library_maturity_model.md#under-development-and-not-yet-ready-for-production-use)

## License

`beman.specgen` is licensed under the Apache License v2.0 with LLVM Exceptions.

## Usage

TODO

Full runnable examples can be found in [`examples/`](examples/).

## Dependencies

### Build Environment

This project requires at least the following to build:

* A C++ compiler that conforms to the C++26 standard or greater
* CMake 3.30 or later
* (Test Only) GoogleTest

You can disable building tests by setting CMake option `BEMAN_SPECGEN_BUILD_TESTS` to
`OFF` when configuring the project.

You can disable building examples by setting CMake option `BEMAN_SPECGEN_BUILD_EXAMPLES` to
`OFF` when configuring the project.

### Supported Platforms

| Compiler   | Version | C++ Standards | Standard Library  |
|------------|---------|---------------|-------------------|
| GCC        | 16-13   | C++26-C++17   | libstdc++         |
| GCC        | 12-11   | C++23-C++17   | libstdc++         |
| Clang      | 22-19   | C++26-C++17   | libstdc++, libc++ |
| Clang      | 18      | C++26-C++17   | libc++            |
| Clang      | 18      | C++23-C++17   | libstdc++         |
| Clang      | 17      | C++26-C++17   | libc++            |
| Clang      | 17      | C++20, C++17  | libstdc++         |
| AppleClang | latest  | C++26-C++17   | libc++            |
| MSVC       | latest  | C++23         | MSVC STL          |

## Development

See the [Contributing Guidelines](CONTRIBUTING.md).

## Integrate beman.specgen into your project

### Build

You can build specgen using a CMake workflow preset:

```bash
cmake --workflow --preset gcc-release
```

To list available workflow presets, you can invoke:

```bash
cmake --list-presets=workflow
```

For details on building beman.specgen without using a CMake preset, refer to the
[Contributing Guidelines](CONTRIBUTING.md).

### Installation

#### Vcpkg

The preferred way to install specgen is via vcpkg. To do so, after installing vcpkg
itself, you need to add support for the Beman project's [vcpkg
registry](https://github.com/bemanproject/vcpkg-registry) by configuring a
`vcpkg-configuration.json` file (which specgen [provides](vcpkg-configuration.json)).

Then, simply run `vcpkg install beman-specgen`.

#### Manual

To install beman.specgen globally after building with the `gcc-release` preset, you can
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
│       └── specgen
│           ├── specgen.hpp
│           └── ...
└── lib
    └── cmake
        └── beman.specgen
            ├── beman.specgen-config-version.cmake
            ├── beman.specgen-config.cmake
            └── beman.specgen-targets.cmake
```

### CMake Configuration

If you installed beman.specgen to a prefix, you can specify that prefix to your CMake
project using `CMAKE_PREFIX_PATH`; for example, `-DCMAKE_PREFIX_PATH=/opt/beman`.

You need to bring in the `beman.specgen` package to define the `beman::specgen` CMake
target:

```cmake
find_package(beman.specgen REQUIRED)
```

You will then need to add `beman::specgen` to the link libraries of any libraries or
executables that include `beman.specgen` headers.

```cmake
target_link_libraries(yourlib PUBLIC beman::specgen)
```

### Using beman.specgen

To use `beman.specgen` in your C++ project,
include an appropriate `beman.specgen` header from your source code.

```c++
#include <beman/specgen/specgen.hpp>
```
