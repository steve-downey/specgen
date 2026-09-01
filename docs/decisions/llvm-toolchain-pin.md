# LLVM toolchain pin for the front end

## Status

Accepted

## Context

The front end builds against LLVM/Clang's C++ API, which is not stable across
major versions. CMake's config search for `find_package(Clang)` globs
`lib/cmake/clang*`, so on a box with several LLVMs installed side by side an
unversioned `find_package(Clang)` silently takes the newest. That is how a new
LLVM major breaks the build the day it lands — LLVM 23, for example, renamed
`ASTContext::getRawCommentForDeclNoCache`, which `frontend.cpp` calls six
times.

A development install verified the front end's needs against LLVM 22 (Clang
headers, the shared `libclang-cpp.so`, the static component libraries, and
CMake package configs for both Clang and LLVM):

```cmake
find_package(Clang REQUIRED CONFIG)   # e.g. -DClang_DIR=/usr/lib/llvm-22/lib/cmake/clang
target_include_directories(… SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
target_link_libraries(… PRIVATE clang-cpp LLVM)
```

Three findings shape the front end's build:

- **The front end builds with GCC.** A probe exercising
  `clang::format::reformat` and `clang::tooling::buildASTFromCodeWithArgs`
  compiles and links under g++, so the front end does not force a Clang-built
  toolchain and rides the existing GCC preset.
- **RTTI follows the LLVM build.** The front end gates `-fno-rtti` on the
  `LLVM_ENABLE_RTTI` the package config exports, never hard-coding either
  setting: distro packages typically build LLVM with RTTI on, while the
  official release binaries build it off — and against a no-RTTI LLVM, our
  own emitted typeinfo would reference base-class typeinfo the libraries
  never define (a link error that surfaces only at `-O0`, where nothing
  dead-strips it).
- **Shared linking is sufficient.** `clang-cpp` plus `LLVM` covers both the
  tooling and formatting surfaces; the static component libraries are present
  but unnecessary.

## Decision

`find_package(Clang REQUIRED)` is unconditional — there is no build without
the Clang front end — and it is **version-pinned**. The cache variable
`BEMAN_SPECGEN_LLVM_VERSION` (default `22.1`) is the version request. Pass
`-DClang_DIR=<prefix>/lib/cmake/clang` for an LLVM off the default search
path; a `Clang_DIR` whose version does not match the request is *rejected*
rather than used.

## Consequences

- Moving to a new LLVM is one deliberate flag
  (`-DBEMAN_SPECGEN_LLVM_VERSION=23.0`) and never an accident of what happens
  to be installed.
- A mismatched `Clang_DIR` fails configuration loudly instead of silently
  building against the wrong headers.
- The LLVM dependency stays confined to the one replaceable front-end target —
  see [ir-boundary](ir-boundary.md).
- Do not reintroduce a configuration option in which the tool cannot generate.
