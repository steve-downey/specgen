# Component recipe

## Status

Accepted

## Context

Components are added regularly, and the build has several registration points
(headers file-set, sources, tests, module umbrella) that must stay in sync. A
fixed mechanical recipe keeps additions uniform and keeps the module lane from
being forgotten.

## Decision

Adding a component `foo` in namespace `beman::specgen::<sub>` is mechanical,
and the module lane means step 7 is not optional:

1. `include/beman/specgen/[sub/]foo.hpp`, guard `BEMAN_SPECGEN_[SUB_]FOO_HPP`.
2. `src/beman/specgen/[sub/]foo.cpp`, including its own header first.
3. `tests/beman/specgen/[sub/]foo.test.cpp`, Catch2, with the re-inclusion
   idempotence check.
4. Add the header to **both** branches of the `FILE_SET HEADERS` list in
   `include/beman/specgen/CMakeLists.txt`.
5. Add the source to `target_sources` in `src/beman/specgen/CMakeLists.txt`.
6. Add `foo` to the `foreach(component …)` list in
   `tests/beman/specgen/CMakeLists.txt`.
7. Add the include to `include/beman/specgen/specgen.hpp` so the module
   umbrella (`specgen.cppm`) exports it.

Subdirectories get their own `CMakeLists.txt` and an `add_subdirectory` from
the immediate parent (`foundation/parse/` and `backend/` are the worked
examples).

## Consequences

- Every component has a header, a source, a Catch2 test with the re-inclusion
  check, and a module export — no partial registrations.
- A missed step is caught structurally: the module umbrella and the test
  `foreach` list fail visibly when a component is absent.
