# specgen — working notes for coding agents

`beman.specgen` is a Clang-based tool that generates C++ standard-library
**description wording** (per [structure.specifications]) from structured headers,
targeting draft-LaTeX / mpark-wg21 / org backends. It is a tool, not a library
(decision `tool-not-library`).

## Read these first (authoritative, in order)

1. **`docs/architecture.md`** — the design reference. Source comments cite its
   sections as `design §N.N`; the section numbering is frozen. §1–§10 describe
   the pipeline, grammar, derivations, entity rules, IR, backends, validation,
   and testing strategy; §11–§12 are the component inventory and cross-cutting
   rules.
2. **`docs/building.md`** — build & verify: presets, the Makefile workflow, the
   LLVM pin, lint, coverage, the examples pipeline, and the current test count.
3. **`docs/decisions/`** — the settled architecture decision records. Source
   comments cite them as `(decision <slug>)`; `docs/decisions/README.md` is the
   index. Rationale lives there; `docs/CODING_RULES.md` decides whether a diff
   is acceptable.

## Build & verify (details in docs/building.md)

One preset, one configuration, 617 tests:

`uv run cmake --preset gcc-release && uv run cmake --build --preset gcc-release && uv run ctest --preset gcc-release`

There is no build without the Clang front end: `find_package(Clang REQUIRED)`
is unconditional and **version-pinned** by `BEMAN_SPECGEN_LLVM_VERSION`
(default `22.1`); a mismatched `Clang_DIR` is rejected rather than used, so
moving to a new LLVM is one deliberate flag and never an accident of what is
installed (decision `llvm-toolchain-pin`). The floor is **C++26** (decision
`cxx26-baseline`), so the dev box needs GCC 16.

Gotchas that bite:

- `make lint` reformats-then-fails on its first run (re-`git add -A` and run
  again).
- Corpus headers (`tests/corpus/*.hpp`) are clang-format-controlled, so
  regenerate goldens after one is reformatted; and golden diagnostics pin
  `file:line`, so edits that add or remove corpus lines move expected files.
- End-to-end smoke: `specgen generate <corpus.hpp>` (single pass, no IR on
  disk), or `specgen generate --emit-ir <corpus.hpp> | specgen render --from-ir -`
  when the IR itself is wanted. The two must agree byte for byte; every
  generate-mode golden's `.singlepass` sibling says so.
- `generate` reports docblock findings on stderr and still produces output on an
  Error; read stderr instead of trusting the exit code.
- The test count above goes stale the moment a ctest case is added; update it
  here **and** in `docs/building.md` together.

## Invariants to check before touching wording behavior

The full statements live in `docs/architecture.md`; headlines only:

- Every corpus header must satisfy design §9's coverage invariant, its leakage
  rule, and the drift/`noexcept` cross-checks (`golden.<case>.validate` gates
  them). `spec_namespace.hpp` and `spec_optional.hpp` carry standing
  findings — pin the diagnostic, never just skip a case.
- A comment line is one of three things: specgen markup (`//!`, `/*!`),
  Doxygen (`///`, `/** */`, dropped from synopses), or draft-form (`//`,
  `/* */`, survives verbatim). See design §1.
- Adding wording to one backend means adding it to all three; a description
  element's label comes from `backend::common::element_label`, not
  `ir::element_name` (design §8 spells out the escape conventions and the
  traps that unit tests alone guard).
- `render --split` fragments belong to the driver, not the backends: a
  fragment *is* a document (design §8). Any change to a backend's
  document-level framing moves `tests/golden/fragments/expected.<backend>/`
  as well as that backend's own goldens.
- Every command in `docs/examples.org` is a script under `examples/cli/`, and
  every output it shows is a captured file checked by `ctest -R examples.`.
  After changing anything a captured file depends on, re-run
  `examples/cli/run-all.sh` from a **non-sanitizer** build
  (`make install-release`) and commit the result.
- Output discipline (decision `format-print-output`): interpolate
  with `std::format`, print with `std::print`/`std::println`; streams move
  bytes that are already built.

## How work is done here

- Changes are PR-sized and end green: full ctest, `make lint`, and (when
  wording output moves) regenerated goldens and captures in the same commit.
- Taste-sensitive output (derived phrasing, FormatStyle) gets a first cut and
  the user's sign-off on the wording before committing.
- If this file and the code ever disagree, trust the code and fix this file.
