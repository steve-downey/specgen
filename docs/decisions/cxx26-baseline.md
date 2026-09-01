# Baseline is C++26

## Status

Accepted

## Context

A C++23 floor is a floor, not a target. The deliverable is the specgen
*program*, not
a portable library (see [tool-not-library](tool-not-library.md)), so the
toolchain matrix owes nothing to hypothetical consumers, and there is
immediate use for C++26 facilities (`std::indirect`, the newer ranges
adaptors, unconditional `<format>` and `<print>`).

## Decision

`cxx_std_26` on core, frontend, and tools. Supported toolchains: **GCC 16,
Clang 22 and 23, all with libstdc++** — a deliberately shallow matrix: CI
keeps one lane per supported compiler. The GCC 15 lane and any libc++/MSVC
ambitions are dropped; `.copier-answers.yml` and the README's platform table
match.

## Consequences

- `std::indirect` is the sanctioned owning-indirection type wherever a
  deep-copying boxed value is wanted (the sibling repos' `Box<A>` is
  explicitly a workaround for its absence — a divergence-log note, not a
  port).
- C++26 ranges adaptors (`views::concat`, `ranges::to_input`, `cache_latest`)
  are available to any conversion.
- The `-std=c++2c` flag passed to clang in `parse_header` is consistent with
  the build.
- The vendored `tree_algorithms` subtree targets C++23 and compiles unchanged
  under 26.
- `<format>` and `<print>` are unconditional on the supported toolchains,
  which is what makes [format-print-output](format-print-output.md) a house
  rule rather than an aspiration.
