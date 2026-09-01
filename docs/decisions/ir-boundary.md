# The IR is the boundary between front end and backends

## Status

Accepted (amended)

> Amended scope: the two tiers exist as an architectural layering, not as build
> configurations; `find_package(Clang REQUIRED)` is unconditional and there is
> one build configuration. The IR boundary itself stands.

## Context

A Clang-tooling front end is a heavy dependency, and the core of the tool (the
IR, the docblock grammar, lowering, the backends, and the IR-level validators)
does not need Clang to be useful. The question is where to draw the seam,
and what the split is for:

- The IR is a real seam rather than a debugging convenience, which is what lets
  the external orgwg21 exporter consume `--emit-ir` output without linking
  Clang.
- Backend and validator tests exercise code that has no LLVM dependency, so the
  majority of the test suite stays fast and does not depend on a 1 GB
  toolchain.
- The LLVM version dependency is confined to one replaceable target.

If those reasons go away, collapse the split instead of maintaining it out of
habit.

## Decision

The codebase divides into two tiers with the IR as the contract between them:

- **Core.** Target `beman.specgen` (a static library). No LLVM dependency.
  Holds the IR, the docblock grammar, lowering, every backend, and every
  validator that operates on IR instead of the AST.
- **Front end.** Target `beman.specgen.frontend`, built against the pinned LLVM
  (see [llvm-toolchain-pin](llvm-toolchain-pin.md)). Holds everything that
  touches `ClangTool`, `Lexer`, `Sema`, and `clang::format`.

The contract between them is exactly `ir::Document`. The front end's only
product is IR; all rendering, ordering, and escaping is core. This keeps the
LLVM version dependency a single replaceable seam, keeps the majority of the
tool testable at the library level, and makes the IR serialization format
load-bearing rather than decorative.

## Consequences

- The IR JSON format is a contract: an external consumer reads `--emit-ir`
  output without linking anything from this repository.
- The clang-independent part is tested in every build; a build configuration
  existing only to test it would prove nothing new, which is why the tiers are
  a code layering rather than a CMake option. Nothing outside this repository needs a
  *binary* that renders but cannot generate, and
  [tool-not-library](tool-not-library.md) already settled that nobody links the
  library either.
- The `render`/`--from-ir` path invokes no Clang at run time even though the
  build always carries the front end
  (see [clang-free-render-mode](clang-free-render-mode.md)).
