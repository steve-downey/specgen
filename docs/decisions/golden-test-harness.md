# Golden test harness

## Status

Accepted

## Context

The tool's primary outputs are text documents (LaTeX, markdown, org, IR JSON).
Byte-exact comparison against checked-in expected files is the natural test
shape, but the harness must be portable and regeneration must never be silent.

## Decision

`tests/golden/<case>/` holds `input.*` and `expected.{tex,md,org,json}`. Cases
register as `add_test` invocations of the `specgen` driver followed by
`cmake -E compare_files`, so the harness has no shell dependency and is
portable across lanes. A `make goldens` target re-runs every case in overwrite
mode; regeneration is wholesale and reviewed in the diff, never silent.

## Consequences

- Golden cases run identically on every platform CMake runs on; no shell
  scripting is involved in comparison.
- An output change surfaces as a reviewable diff over the whole golden set
  rather than as an in-place mutation nobody sees.
- Expected files are the contract for output bytes, including the IR JSON
  serialization.
