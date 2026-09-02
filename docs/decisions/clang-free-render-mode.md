# The driver has a clang-free render mode

## Status

Accepted (amended)

> Amended scope: there is no clang-free *build* — every build carries the front
> end — but `render --from-ir` still invokes no Clang at run time, and that
> path stands.

## Context

Rendering a fragment from serialized IR requires none of the Clang front end;
only extracting IR from a header does. The driver's subcommand split makes that
visible at the command line and keeps the backends, the validators, and the
fragment split testable at the library level, independent of LLVM.

## Decision

The executable `specgen`, from `tools/specgen/main.cpp`, has two subcommands
split along the IR boundary (see [ir-boundary](ir-boundary.md)):

- `specgen render --from-ir <file.json> --backend {latex,mpark,org}` — core
  code only, and the reason a *rendered* fragment needs no compiler at run
  time.
- `specgen generate <header.hpp> [--emit-ir] [--backend …]` — front-end code,
  driving Clang.

## Consequences

- A consumer holding IR JSON — including the out-of-tree orgwg21 exporter
  pipeline — renders it without any compiler at run time.
- `render` remains core **code**: the backends, validators and the fragment
  split are testable at the library level without touching Clang, which is the
  seam the IR names — but it is not a separate build configuration.
- The split is about *Clang at run time*, not about a file the pipeline must
  pass through: `generate` without `--emit-ir` runs the front end and a backend
  in one pass, with the IR staying in memory. Both commands share one back half
  (`emit_wording` in the driver), so the wording is byte-identical either way —
  each generate-mode golden's `.singlepass` sibling demands exactly that.
  `--emit-ir` remains how the IR is *serialized*, for a consumer that will
  render it later or elsewhere.
- The driver is built only where the front end is, so all four of its
  subcommand help texts are the same in every build; there are no stub
  subcommands that differ by build flavor.
