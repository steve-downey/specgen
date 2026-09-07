# LLVM toolchain pin for the front end

## Status

Accepted

## Context

The front end builds against LLVM/Clang's C++ API, which is not stable across
major versions. CMake's config search for `find_package(Clang)` globs
`lib/cmake/clang*`, so on a box with several LLVMs installed side by side an
unversioned `find_package(Clang)` silently takes the newest. That is how a new
LLVM major breaks the build the day it lands — LLVM 23 renamed
`ASTContext::getRawCommentForDeclNoCache` out from under `frontend.cpp`.

A development install verified the front end's needs against LLVM 22, and the
23 move re-confirmed all three findings below unchanged (Clang headers, the
shared `libclang-cpp.so`, the static component libraries, and CMake package
configs for both Clang and LLVM):

```cmake
find_package(Clang REQUIRED CONFIG)   # e.g. -DClang_DIR=/usr/lib/llvm-23/lib/cmake/clang
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
`BEMAN_SPECGEN_LLVM_VERSION` (default `23.1`) is the version request. Pass
`-DClang_DIR=<prefix>/lib/cmake/clang` for an LLVM off the default search
path; a `Clang_DIR` whose version does not match the request is *rejected*
rather than used.

The pin is a **floor as well as a ceiling**: the front end is written against
exactly one LLVM, not against a supported range. There are no version `#if`s
in `frontend.cpp` and none are to be added — the tree builds against the
pinned major.minor and nothing else. Getting a matching LLVM is not a burden
worth carrying compatibility code for: apt.llvm.org publishes a per-release
channel for every supported LLVM, and the official release tarballs (what CI
unpacks) cover the rest.

## Consequences

- Moving to a new LLVM is one deliberate flag
  (`-DBEMAN_SPECGEN_LLVM_VERSION=24.1`) and never an accident of what happens
  to be installed. Exercised once, moving 22 → 23: the pin turned an API
  rename into a one-identifier change plus a formatter fix, found by building
  rather than by a broken CI run. `docs/plans/llvm-23-port.md` is the record.
- A contributor whose distro packages an older LLVM installs the pinned one
  from apt.llvm.org or the release tarball; the tree does not build against
  the older one, by design
  (`docs/plans/llvm-23-port.md#llvm-22-support-window`).
- A mismatched `Clang_DIR` fails configuration loudly instead of silently
  building against the wrong headers.
- The LLVM dependency stays confined to the one replaceable front-end target —
  see [ir-boundary](ir-boundary.md).
- Do not reintroduce a configuration option in which the tool cannot generate.
