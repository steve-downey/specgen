# Hermetic corpus policy

## Status

Accepted

## Context

The tool targets Beman-style headers such as `bemanproject/optional` and
`bemanproject/expected`, and its output is measured against real working-draft
LaTeX. Depending on those moving upstream sources directly would make the test
suite non-hermetic and hostage to their churn.

## Decision

`tests/corpus/` holds hand-curated Beman-style headers written for this tool.
Upstream `bemanproject/optional` and `bemanproject/expected` are **not**
submoduled and the working draft is **not** vendored: tests stay hermetic, and
the acid test against real draft LaTeX is an out-of-tree comparison, not a
build dependency.

## Consequences

- The test suite runs from a clean checkout with no network access and no
  external repositories.
- Corpus headers are fixtures the project controls, so they can be curated to
  exercise specific tool behavior.
- Measuring against the real upstream headers and the real draft is a
  deliberate, documented out-of-tree procedure, never a ctest case.
