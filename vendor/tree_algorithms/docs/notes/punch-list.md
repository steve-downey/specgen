# Punch list — release-gate sweep (Task 5.1), finalized 2026-07-06

Reorganized from the running Task 1.2/2.1/4.2/4.3/4.4 punch list into
resolved / needs-Steve / mechanical, folding in the Task 5.1 audit sweep
(local build matrix across gcc-14/15/16 and clang-18..22, header
verification, DEV-03/DEV-04/Decision-2 greps, GitHub CI diagnosis, README
review).

## RESOLVED

- GitHub repo created and pushed to `steve-downey/tree_algorithms`; `main` tracks `origin/main`. Done 2026-07-05 (cbdc041).
- `cmake/clang-flags.cmake` and `cmake/gcc-flags.cmake` pinned `-std=gnu++20`, below the deducing-this floor; bumped to `gnu++23`. Flagged in c2080d0, fixed in 3cfb506; confirmed by this sweep — all 8 locally-buildable toolchains (gcc-14/15/16, clang-18/19/20/21/22) now configure and link against `gnu++23`.
- `cmake/llvm-toolchain.cmake` and `cmake/ci-clang-toolchain.cmake` hard-coded `-std=c++20`; current file contents show both already pinned to `-std=c++23`. No longer an open item.
- Required matrix is C++23-only by design; C++26 forward-compatibility is covered by an allowed-to-fail `advisory-cpp26` job in `.github/workflows/ci_tests.yml` (gcc, `-DCMAKE_CXX_STANDARD=26`, `continue-on-error: true`). The old "flag if C++26 should be required instead" question is answered: advisory only, intentionally.
- `vcpkg-configuration.json`/`port/` port templates: `port/vcpkg.json.in` and `port/portfile.cmake.in` exist and the `vcpkg-ci / Test port templates {}` (default, no features) job passes in CI (run 28801179604). The old "port does not exist" prediction is stale for the default feature set — see OPEN/needs-Steve below for the surviving `modules` feature-combination failure, which is a different, still-live problem.
- Local matrix sweep (Task 5.1, this pass): gcc-14, gcc-15, gcc-16, clang-18, clang-19, clang-20, clang-21, clang-22 — all 53/53 tests pass. `make TOOLCHAIN=gcc-16 compile-headers` and `make TOOLCHAIN=clang-22 compile-headers` both verify all 13 interface headers cleanly.
- DEV-03 (bare CTAD on wrapped template types `Fix`/`Box`/`Succ`/`Cons`/`Node`), DEV-04 (CRTP base constexpr-ness + static_assert coverage in `functor.hpp`/`functor.test.cpp`), and Decision 2 (`cata`/`ana`/`hylo` banned as identifiers) audits: zero violations found in `include/`, `tests/`, `examples/`. Details below under mechanical (recorded, not actionable).

## OPEN — needs Steve

- ~~Request a real WG21 paper number to replace the `DnnnnR0` placeholder.~~ **RESOLVED 2026-07-17**: the paper is D4322R0, "Algorithms for Trees"; the draft moved to `papers/algorithms-for-trees.md` (DnnnnR0 file removed), README and DECISIONS.md amendment updated.
- Decide whether to upstream copier-template fixes to `steve-downey/exemplar`. Candidates seen so far: the `gnu++20`→`gnu++23` flag bump (already template-sourced per prior sweep), and the stale `BEMAN_EXEMPLAR_USE_MODULES` guard at `tests/beman/tree_algorithms/CMakeLists.txt:11` (see mechanical below) — the latter is exactly the kind of copier leftover that should be fixed in the template itself so future `exemplar`-derived repos don't inherit it.
- README/registry owner swap: decide whether `steve-downey/tree_algorithms` is this repo's permanent home, or whether it is destined to move into the `bemanproject` org. Everything below is currently pointed at `bemanproject` and needs a decision, not just an edit:
  - CI/Lint/Coverage badges, `README.md:8-12`.
  - `vcpkg-configuration.json` registry entry (`bemanproject/vcpkg-registry`) and the matching prose at `README.md:~86`.
  - `.github/ISSUE_TEMPLATE/*.md` and `.github/pull_request_template.md`, which link to `bemanproject/beman` docs.
  - If the repo stays under `steve-downey` permanently, do a global rename; if it's moving to `bemanproject` later, leave as-is but say so explicitly in the README so the mismatch doesn't read as a bug.
- clang-18 floor viability: in CI, both `clang 18 c++23 ... Release.Default` jobs (libc++ and libstdc++) fail at compile time, not at config time. clang 18 reports `Fix<NatF>` (and other `Fix<F>` instantiations) as a non-literal type in a constant expression — `constexpr function never produces a constant expression [-Winvalid-constexpr]` in `tests/beman/tree_algorithms/fix.test.cpp:32` and `tests/beman/tree_algorithms/recursion_schemes.test.cpp:203/208` — rejecting the `Box<A>` pattern (constexpr but non-trivial destructor, `include/beman/tree_algorithms/box.hpp:60`) that clang 19-22 and all three tested gcc versions accept. Needs a decision: bump the required floor from clang-18 to clang-19, or accept/waive clang-18 as broken. Could not reproduce locally in this sweep — see the matching mechanical note about the local libc++ version mismatch, which fails earlier and masks this bug on this machine.
- Modules build (`-DBEMAN_TREE_ALGORITHMS_USE_MODULES=On`) is red across the board in CI, not just "unexercised" as previously noted: `gcc 15`, `gcc 16`, `clang 22 libstdc++`, and `clang 22 libc++` build-and-test jobs all fail (see mechanical for exact errors), and the `vcpkg-ci` `modules:true` feature combination fails the same way. Decide whether modules ship as advisory/best-effort for R0 or block the release until the `import std` / implicit-std-header double-declaration conflict is resolved. This supersedes the old "add a CI matrix entry" item — the entry now exists (per Task 2.1's note) and it's failing everywhere it runs.

## OPEN — mechanical follow-ups

- README badges (`README.md:8-12`) and the vcpkg registry reference (`vcpkg-configuration.json`, `README.md:~86`) still say `bemanproject/tree_algorithms` / `bemanproject/vcpkg-registry`; swap to `steve-downey/...` once the needs-Steve owner-swap decision above is made.
- ~~`README.md:39` still says "(Test Only) GoogleTest".~~ **RESOLVED 2026-07-17**: bullet now says Catch2.
- ~~README copier-template placeholders (description, Implements line, Usage section).~~ **RESOLVED 2026-07-17**: real description, Implements pointing at D4322R0, and a Usage section with a code sketch and the annotated example list.
- `.github/ISSUE_TEMPLATE/*.md` (`implementation-deficiency.md`, `infrastructure-issues.md`, `paper-discussion.md`) and `.github/pull_request_template.md` link to `bemanproject/beman` docs — cosmetic, low priority, only swap once the owner-swap decision above lands.
- `tests/beman/tree_algorithms/CMakeLists.txt:11` guards the `todo` test's `CXX_MODULE_STD` property with `BEMAN_EXEMPLAR_USE_MODULES` (undefined in this project — wrong copier project name), so that one target's module property silently never applies; every other guard in the same file (lines 24, 37, 53, 69, 82, 98, 114, 127, 143) correctly uses `BEMAN_TREE_ALGORITHMS_USE_MODULES`. One-line rename fix: `BEMAN_EXEMPLAR_USE_MODULES` → `BEMAN_TREE_ALGORITHMS_USE_MODULES`.
- Lint Check (pre-commit) is red in CI: beman-tidy's `file.license_id` check requires an SPDX line in the first 25 lines of every file. Currently missing from (verified against the working tree, not just the CI log):
  - `.github/workflows/codeql.yml`
  - `.github/workflows/doxygen-gh-pages.yml`
  - `.github/workflows/ossf-scorecard-analysis.yml`
  - `cmake/ci-clang-toolchain.cmake`
  - `cmake/clang-18-toolchain.cmake`
  - `cmake/clang-19-toolchain.cmake`
  - `cmake/clang-flags.cmake`
  - `cmake/gcc-14-toolchain.cmake`
  - `cmake/gcc-15-toolchain.cmake`
  - `cmake/gcc-16-toolchain.cmake`
  - `cmake/gcc-flags.cmake`
  - `cmake/gcc-toolchain.cmake`
  - `cmake/llvm-master-toolchain.cmake`
  - `cmake/llvm-toolchain.cmake`
  - `cmake/toolchain.cmake`
  - `cmake/use-fetch-content.cmake`
  - `cmake/x64-linux-custom.cmake`

  `cmake/clang-20-toolchain.cmake`, `cmake/clang-21-toolchain.cmake`, and `cmake/clang-22-toolchain.cmake` already carry the header (`# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception` as a `#`-comment near the top) — use those three as the copy-paste template for the rest.
- Doxygen GitHub Pages Deploy Action fails in CI with `error: configuration file docs/Doxyfile not found!`; the workflow (`.github/workflows/doxygen-gh-pages.yml`) references `config_file: docs/Doxyfile`, which was never generated/committed. Either generate and commit a `docs/Doxyfile` (`doxygen -g docs/Doxyfile`, then set `PROJECT_NAME`/`INPUT` etc. to match this repo), or remove/disable the workflow if Doxygen docs aren't wanted yet for R0.
- Local dev-environment note (not a repo defect, but explains a gap in this sweep's coverage): this machine has only `libc++-23-dev` installed system-wide (no per-version `libc++-18-dev`..`libc++-21-dev`); clang-18/19/20 with `-stdlib=libc++` (forced by `cmake/clang-flags.cmake`) pick up the mismatched libc++-23 headers and fail during vcpkg's Catch2 build with `#pragma clang attribute` / `__visibility__` errors — an environment problem, not a code problem. clang-21 and clang-22 happen to build fine locally against libc++-23. Because of this, the clang-18 build-and-test failure documented above (needs-Steve) could only be confirmed via the GitHub Actions logs, not reproduced locally; if Steve wants to repro locally, install matching versioned `libc++-<N>-dev` packages or run inside the same container image CI uses (`ghcr.io/bemanproject/infra-containers-*`).
- Modules build failures (detail for the needs-Steve decision above), for whoever picks this up:
  - `gcc 15`/`gcc 16` (`libstdc++`, modules Debug) and the `vcpkg-ci` `modules:true` job: `error: redeclaring 'std::size_t' in module 'beman.tree_algorithms' conflicts with import` (and similarly for `ptrdiff_t`, `nullptr_t`, `__terminate`) — a non-module standard header is getting `#include`d somewhere reachable from the module interface unit alongside `import std;`.
  - `clang 22 libstdc++` (modules Debug): `error: declaration of '__is_constant_evaluated' in the global module follows declaration in module beman.tree_algorithms` — same family of problem.
  - `clang 22 libc++` (modules Debug): different symptom, same root cause family — `type alias template redefinition with different types` in `<__type_traits/promote.h>`, plus `declaration of 'visit' must be imported from module 'std' before it is required` in `examples/expression_algorithms.cpp:54` (a non-module `#include` reaching `std::visit` before/instead of `import std;`).
- Foldable/Traversable article material (source lines 572-796) earmarked for a follow-up article; not in the minimal core. (Deferred by design, carried forward, not blocking R0.)
- Vendor the `papers/wg21` Pandoc framework (git subtree, as in the fixpoint repo) when the D-paper needs real builds; the draft lives in `papers/` as plain Pandoc-WG21 markdown until then. (Deferred by design, carried forward, not blocking R0.)
- Verify P2688/P1371 pattern-matching status against the current WG21 paper trail before publication (text hedged "as of this writing"); re-check the `std::indirect` paper number cited in the D-paper References (P3019) before publication; re-measure all `# MEASURE:`/`<!-- MEASURE -->` flags in `docs/notes/engineering-reality-sections.md` against `beman.tree_algorithms` before quoting numbers. (Carried forward verbatim from Task 4.2/4.3, still unresolved, not touched by this sweep.)
- No org-export verification possible in this environment: no `.emacs.d`/export target and org-transclusion isn't installed; mirror `trees/main`'s export setup if HTML/PDF output is wanted for the paper/article. (Carried forward from Task 4.4.)
- Paper synopsis is a manually-diffed mirror of the headers; re-diff on any signature change (HTML comment in the paper flags this). `unfold_fix`/`refold` explicit-fmap overloads in `recursion_schemes.hpp` are unanchored; anchor if a future revision needs them. (Carried forward from Task 4.4.)

## Summary

- RESOLVED: 7
- OPEN — needs Steve: 5
- OPEN — mechanical: 14 (7 new/updated from this sweep's audit + CI diagnosis, 7 carried forward unchanged from prior tasks)
