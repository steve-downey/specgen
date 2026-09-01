# This is a tool, not a library

## Status

Accepted

## Context

`beman.specgen` inherited a Beman exemplar project skeleton whose build-and-test
matrix — GCC 11–14, Clang 17–21, AppleClang, MSVC, C++17/C++20 — assumed a
library consumed by arbitrary toolchains. That assumption does not describe this
project: it is an executable that generates C++ standard-library description
wording.

## Decision

`beman.specgen` is an executable that generates wording; it is not a library
consumed by arbitrary toolchains. Everything else in the project's structure
follows from that, and it is what justifies a deliberately narrow supported
toolchain set — narrowed further by [cxx26-baseline](cxx26-baseline.md) to
**GCC 16 and Clang 22–23 on C++26**.

## Consequences

- The inherited exemplar matrix is removed, taking the build-and-test matrix
  from 96 configurations to 29 and the preset lanes from 8 to 4 (and further
  reduced since by [cxx26-baseline](cxx26-baseline.md)).
- Nobody links the core as a library; the tool's command-line surface is the
  product.
- The packaging apparatus (vcpkg port, install/export rules, header
  `FILE_SET`s) is an inheritance from the consumable-library assumption and
  awaits revisiting.
