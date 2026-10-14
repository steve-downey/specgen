# Plan: a downloadable `specgen` binary (manylinux_2_34, x86_64)

**Status:** DRAFT, 2026-10-12. Every question that was the user's to decide is decided; the one still open,
[llvm-from-release-tarball](#llvm-from-release-tarball), is a measurement and the first act of execution. Nothing
below has landed. The measurements are from this date, on the dev box
(Ubuntu, GCC 16.0.1 snapshot, LLVM 23.1.3 from apt.llvm.org) and in throwaway containers; each one records the command
that produced it so a later reader can redo it rather than trust it.

## Goal

Anyone on a current x86_64 Linux should be able to download one archive, unpack it, and run `specgen generate` on a
header, without GCC 16, without LLVM 23, and without building anything. The first delivery vehicle is GitHub: **every
push to `main` cuts a GitHub Release** carrying the archive, because `main` is always releasable and "live at head" is
the policy for now; release branches and version tags can come much later. The second, later, is a wheel on PyPI, so
`uv tool install beman-specgen` or `pipx install beman-specgen` works; the wheel is also the cheapest way to *prove*
the binary's compatibility claim, so it is built from the start and published only at the end.

The compatibility claim is **`manylinux_2_34_x86_64`** (PEP 600): glibc 2.34 or newer, which is Ubuntu 22.04+,
Debian 12+, Fedora 35+, RHEL 9+ and their derivatives. That is less ancient than the ecosystem default
(`manylinux_2_28`, glibc 2.28, 2018) and less exclusive than the glibc of the newest Ubuntu LTS (2.39), and it is the
newest floor for which PyPA ships an x86_64 build image. The reasoning is in [why-2-34](#why-2-34).

Two project decisions constrain everything here: the toolchain floor is GCC 16 on C++26 and nothing older
([cxx26-baseline](../decisions/cxx26-baseline.md)), and the Clang front end is built against exactly LLVM 23.1
([llvm-toolchain-pin](../decisions/llvm-toolchain-pin.md)). Neither is available as a package in any glibc-2.34
distribution's base repositories, which is what makes this a plan and not a workflow file.

## What the binary is today

The `gcc-release` driver is 96 MB of RelWithDebInfo and dynamically linked to fifteen libraries beyond glibc:

```sh
ldd build/gcc-release/tools/specgen/specgen
#   libclang-cpp.so.23.1  libLLVM.so.23.1  libstdc++.so.6  libgcc_s.so.1  libffi.so.8  libedit.so.2
#   libz3.so.4  libz.so.1  libzstd.so.1  libxml2.so.16  libtinfo.so.6  libbsd.so.0  libmd.so.0  libm  libc
```

Every one of those except glibc's own has to go: libclang-cpp and libLLVM because no target machine has LLVM 23,
libstdc++ because no target machine has GCC 16's, and the rest because they are LLVM's optional dependencies, which the
LLVM we build can simply not have. What remains must be inside the `manylinux_2_34` policy's whitelist (libc, libm,
libdl, libpthread, librt, libgcc_s, libstdc++ at EL9's symbol versions, libz).

### The resource directory is broken already

Clang's tooling entry point `buildASTFromCodeWithArgs` names the compiler `clang-tool` and derives the resource
directory (the builtin headers: `stddef.h`, `stdarg.h`, …) from that name. With no path in it, the result is the
*relative* path `lib/clang/23`, resolved against the current directory. Observed with the driver copied to a scratch
`bin/` and run on a header that includes `<cstddef>` and `<optional>`:

```sh
specgen generate widget.hpp --no-compile-commands -- -v 2>&1 | grep -E 'InstalledDir|resource-dir|clang/23'
# InstalledDir:
#  ... "-resource-dir" "lib/clang/23" ...
#  /usr/include/clang/23.1.3/include     <- the only place stddef.h was found
```

The parse succeeds on the dev box only because Debian's `libclang-cpp` bakes `/usr/include/clang/23.1.3/include`
in as a fallback search directory. A machine without LLVM 23 has no such directory, so a shipped binary would fail to
parse any header that reaches `<cstddef>`, which is every header that includes anything from the standard library. The
corpus never noticed because every corpus header is self-contained
([hermetic-corpus](../decisions/hermetic-corpus.md)).
Fixing this does not wait for CI; it is [resource-dir-discovery](#resource-dir-discovery), and it lands first.

The same trace shows where the *standard library* headers come from: Clang's driver scans `/../lib/gcc/<triple>/*`
(the empty `InstalledDir` is what puts the leading `/..` there, and merged-`/usr` makes it work) and selects the newest
GCC installation it finds. On the target machine that is the user's own g++, whose libstdc++ headers then serve
`<optional>`. That is the right behaviour and it stays: the shipped binary needs **a host g++ installation for headers
that include the standard library, and nothing at all for self-contained ones**. It becomes a documented runtime
requirement rather than an accident.

## [why-2-34](#why-2-34)

**Why `manylinux_2_34`, and what it is built on.**

The glibc floors on the table, what each would exclude, and what each costs to build on:

| Tag | glibc | Excludes | Build image |
| --- | --- | --- | --- |
| `manylinux_2_28` | 2.28 | nothing anyone runs | AlmaLinux 8, PyPA image, gcc-toolset up to 14 |
| `manylinux_2_34` | 2.34 | Ubuntu 20.04, Debian 11, RHEL 8 | AlmaLinux 9, PyPA image, gcc-toolset-16 from 9.9 |
| `manylinux_2_39` | 2.39 | Ubuntu 22.04, Debian 12, RHEL 9 | AlmaLinux 10; **no PyPA x86_64 image**; x86-64-v3 |
| `manylinux_2_43` | 2.43 | everything but Ubuntu 26.04 | not in auditwheel's policy table |

`manylinux_2_39` was the first choice (Ubuntu 24.04's glibc) and lost on two facts. PyPA publishes that flavour for
aarch64 and riscv64 only; `quay.io/pypa/manylinux_2_39_x86_64` does not exist. And EL10 raised its x86_64 baseline to
x86-64-v3, so the pieces a gcc-toolset links statically (below) would carry AVX2 code, which is very likely *why* there
is no x86_64 image. `manylinux_2_34` keeps us on a PyPA image that is rebuilt almost daily (`2026.10.10-1` on the day
of writing) and that cibuildwheel pins as a first-class choice; its README still says "ALPHA", which records PyPA's
posture about promoting it to the default, not the image's health.

**The compiler comes from the distribution, not from source.** Red Hat's GCC Toolsets ship a current GCC in AppStream
and link `libstdc++_nonshared.a` into every binary: the parts of the new libstdc++ that the system's old
`libstdc++.so.6` lacks travel inside the executable, and the executable depends only on the system library's symbol
versions (GCC 11's `GLIBCXX_3.4.29` on EL9). That is exactly the compatibility shim a portable binary wants, and it
means **no `-static-libstdc++`**. Availability on the day of writing:

- AlmaLinux 9 stable AppStream: `gcc-toolset-15` (15.2.1). `gcc-toolset-16` (16.2.1, binutils 2.46) is in the
  AlmaLinux **9.9 beta**; AlmaLinux publishes toolsets in the ordinary AppStream repository, so it reaches the PyPA
  image's `dnf` at 9.9 GA with no image change. CentOS Stream 9 already carries it.
- Ubuntu 24.04 with `ppa:ubuntu-toolchain-r/test`: `g++-16` is a **trunk snapshot** (`16-20260315`, r16-8100), older
  than the 16.1 release and without `-static-libstdc++` it would need libstdc++ 16 on the target. Measured; rejected.
- The beman `infra-containers-gcc:16` image CI builds in is Gentoo-based with a newer glibc; a binary built there
  carries that glibc's floor. Unusable for any manylinux tag.

**LLVM is built from source, once per pin, into a layered image.** No glibc-2.34 distribution packages LLVM 23, and the
official release tarballs are built on a newer glibc and ship no static `clang-cpp`. The LLVM build is the one
genuinely expensive step (hours on a 4-vCPU runner), so it lives in its own image layer that is rebuilt only when the
pin or the Dockerfile changes, never per specgen commit. That is [toolchain-image](#toolchain-image).

**Runner budget.** Public repositories get `ubuntu-latest` at 4 vCPU / 16 GB, free and unlimited, with a 6-hour job
limit; arm64 runners are also free for public repositories, which is what makes an aarch64 lane cheap later. The
specgen tree itself costs about 2300 CPU-seconds for the `gcc-release` configuration (`.ninja_log`), so the per-commit
release build is ten to fifteen minutes plus a large static link.

## Steps

In reading order, each landing green on its own ([how work is done here](../../AGENTS.md)):
[resource-dir-discovery](#resource-dir-discovery) and [static-llvm-link](#static-llvm-link) are ordinary PRs against
the tree with no CI consequences; [toolchain-image](#toolchain-image) comes next;
[release-build-workflow](#release-build-workflow) uses it; [docs-and-records](#docs-and-records) and
[pypi-wheel](#pypi-wheel) close. The order is reading order; the slug is the identity.

### [resource-dir-discovery](#resource-dir-discovery)

Make the driver carry its own builtin headers and find them from its own location.

- `parse_header` (frontend.cpp) appends `-resource-dir=<dir>` **unless** the resolved arguments already contain a
  `-resource-dir` (a caller's `--` tail or compile command wins, which is the existing precedence rule). `<dir>` is
  `<executable dir>/../lib/clang/<LLVM major>`, from `llvm::sys::fs::getMainExecutable`; the major comes from the
  pinned LLVM at configure time (`LLVM_VERSION_MAJOR`), as a compile definition on the frontend target.
- The build tree mirrors the install layout so the build-tree driver stops depending on a distro's baked-in fallback:
  a custom command places a symlink (or copy) of the pinned LLVM's `lib/clang/<major>` at
  `$<TARGET_FILE_DIR:specgen>/../lib/clang/<major>`.
- The install rule adds `lib/clang/<major>/include` to the `specgen_Runtime` component, sourced from the pinned LLVM's
  resource directory (`${LLVM_LIBRARY_DIR}/clang/<major>/include`; the exact variable is to be confirmed against the
  package config, which differs between the Debian layout and an upstream install).
- Tests: a header `tests/corpus/builtin_include.hpp` that includes `<stddef.h>`, parsed with `-- -nostdlibinc` so that
  *only* the resource directory can satisfy it; and a case that runs `generate -- -v` and asserts the `-resource-dir`
  it reports is absolute and exists. Both run against the build-tree driver. The ctest count moves; `AGENTS.md` and
  `docs/building.md` move with it.
- Docs: `user-guide.md` "Requirements" gains the runtime requirement (host g++ headers for standard-library includes;
  nothing for self-contained headers), and the `--gcc-install-dir=`/`--gcc-toolchain=` tail flags as the way to point
  at a particular one.

Done when the relocated-binary reproduction above reports an absolute, existing resource directory and parses
`<cstddef>` with `/usr/include/clang` renamed away.

**Landed** (2026-10-13), with three differences from the bullets above, each found while doing it:

- The directory comes from Clang's own `clang::GetResourcesPath(argv0, addr)` (`clang/Options/OptionUtils.h`), the
  function the `clang` driver itself uses, rather than a hand-assembled path and a `LLVM_VERSION_MAJOR` compile
  definition; it is computed once and passed **only when it exists**, so a unit-test binary with no sibling `lib/`
  keeps Clang's default. It is placed ahead of the resolved arguments, so a caller's `-resource-dir` wins by last-wins
  parsing, the same rule the `-std` default already relies on. The function lives in `libclangOptions.a`, which
  [static-llvm-link](#static-llvm-link)'s component list must therefore include.
- The install destination is the literal `lib/clang/<major>/include`, not `CMAKE_INSTALL_LIBDIR`: Clang's rule says
  `lib`, and GNUInstallDirs says `lib64` on EL9. The source is `${LLVM_LIBRARY_DIR}/clang/${LLVM_VERSION_MAJOR}`,
  which is right for both the Debian layout and an upstream install.
- `golden.builtin_include` (`-nostdlibinc`) passes on Debian even with the resource directory gone, because Debian's
  baked-in `/usr/include/clang/<version>/include` survives `-nostdlibinc`; `driver.resource-dir`, which reads back
  the `-resource-dir` Clang was given, is the case that fails there. The reproduction now shows that fallback absent
  from the search list altogether, which is the "renamed away" condition without renaming anything.

### [static-llvm-link](#static-llvm-link)

Let the front end link LLVM and Clang statically when the LLVM it is given has no shared libraries.

- The driver installs LLVM's crash handler at startup (`llvm::sys::PrintStackTraceOnErrorSignal`) and prints the
  version and commit above the trace. A fully stripped binary has no symbol table, so the trace is a list of
  `specgen(+0x…)` offsets; those, the header the user was running on, and the release's `.debug` asset are what a
  report needs to be debugged offline ([debug-info-shipping](#debug-info-shipping)). This is a few lines in
  `tools/specgen/main.cpp` and belongs with the link change because it is the first build whose binary is stripped.

- `src/beman/specgen/frontend/CMakeLists.txt`: a cache option `BEMAN_SPECGEN_LLVM_LINK` with values `auto`
  (default), `shared`, `static`. `shared` is today's `clang-cpp LLVM`. `static` links the Clang component targets the
  front end actually uses (`clangTooling clangFormat clangFrontend clangSerialization clangDriver clangParse clangSema
  clangAnalysis clangEdit clangRewrite clangLex clangAST clangBasic clangSupport` plus `clangToolingCore` and
  `clangToolingInclusions`), letting the exported `ClangTargets.cmake` pull the LLVM component libraries transitively.
  `auto` picks `shared` when `TARGET clang-cpp` exists and `static` otherwise.
- `-ffunction-sections -fdata-sections` on the frontend and `-Wl,--gc-sections` on the driver in the static case; the
  RTTI gate stays as it is (it already follows `LLVM_ENABLE_RTTI`).
- Exercised **on the dev box first**: Debian ships the 333 static component archives alongside the shared libraries,
  so `-DBEMAN_SPECGEN_LLVM_LINK=static` builds and runs the full suite here before any container exists. `ldd` of the
  result must show no `libclang-cpp` or `libLLVM`; the z3/xml2/edit/ffi tail will still be there on Debian's LLVM and
  disappears only with our own ([toolchain-image](#toolchain-image)).
- The clang-tidy plugin (`tools/tidy`) is **not** touched: it loads into the pinned `clang-tidy` and stays
  shared-linked; in the release image there is no clang-tidy, the probe comes up empty, and `style.*` is excluded
  explicitly ([style-gate-in-release-job](#style-gate-in-release-job)).

**Landed** (2026-10-13), with these differences from the bullets above:

- The `ldd` expectation for the dev box was wrong. Debian builds LLVM with `LLVM_LINK_LLVM_DYLIB`, and its exported
  static `clang*` targets name the shared `LLVM` in their link interface, so `static` against apt.llvm.org's LLVM gives
  static Clang **plus `libLLVM.so`**: no `libclang-cpp`, but not LLVM-free. Naming the static LLVM components as well
  would put two LLVMs in one process. The option therefore chooses only how *Clang* is linked; LLVM follows from the
  package. Verified both ways: the Debian `static` driver loads `libLLVM.so.23.1` and generates correctly; the
  tarball's targets name static `LLVM*` components and the driver links no LLVM library at all.
- The static component list gains `clangOptions` (home of `clang::GetResourcesPath`, from
  [resource-dir-discovery](#resource-dir-discovery)).
- `-Wl,--gc-sections` is an `INTERFACE` link option of the front-end library, so it reaches the driver and the
  front-end unit tests alike.
- The crash handler is `frontend::install_crash_handler`, called first thing in `main`: the driver includes no LLVM
  header, by [ir-boundary](../decisions/ir-boundary.md), so the LLVM call lives behind the front-end seam. The banner is
  registered as a signal handler ahead of `PrintStackTraceOnErrorSignal`'s and written with `write(2)`. Checked by
  sending `SIGSEGV` to a running `render --from-ir -`; there is no ctest case for it.

**The link measurement on the dev box** (the fourth question of
[llvm-from-release-tarball](#llvm-from-release-tarball), short of the image), 2026-10-13: `static` against the
unpacked `LLVM-23.1.3-Linux-X64` tarball, built by GCC 16.0.1, links with no undefined symbols in 4 min 52 s wall
(1790 CPU-seconds, 8 jobs) and passes every ctest case but `style.no-raw-loops`, which fails as designed with no
`clang-tidy` beside that LLVM (978 of 979; the ten tidy-plugin cases are not registered without it). The driver:

```sh
ldd specgen          # libz, libstdc++, libm, libgcc_s, libc: nothing outside the manylinux whitelist
objdump -T specgen | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1     # GLIBC_2.34
objdump -T specgen | grep -o 'GLIBCXX_[0-9.]*' | sort -Vu | tail -1   # GLIBCXX_3.4.31 (dev box GCC 16's libstdc++;
                                                                      # the EL9 nonshared shim is the image's job)
size specgen         # text 37.8 MB, data 1.5 MB; 145 MB with DWARF
strip --strip-unneeded specgen   # 39.3 MB; xz -T0: 10.5 MB
nm -D specgen | grep -i zstd     # only the weak ZSTD_trace_* hooks: LLVM's Findzstd took libzstd.a
```

zstd resolving from Debian's `libzstd.a` confirms the `base` layer needs a static zstd built from source, since EL9
has no `libzstd-static`. Size lands inside the 35–50 MB / 10–12 MB estimate.

### [toolchain-image](#toolchain-image)

Two Dockerfiles under `infra/release/` and one workflow, `.github/workflows/toolchain-image.yml`, that publishes two
images to `ghcr.io/steve-downey/specgen-toolchain`, each tagged by content and referenced downstream by digest:

- **`base`** — `FROM quay.io/pypa/manylinux_2_34_x86_64@sha256:…` (a dated tag, pinned by digest). Adds
  `gcc-toolset-16`, `git`, `ninja-build`, and `uv`, with CMake coming from `uv` exactly as the dev build does (the
  `pyproject.toml` pin). `/opt/rh/gcc-toolset-16/enable` is sourced in the image's `ENV`/`ENTRYPOINT` so `cc`/`c++`
  are GCC 16 for every later layer and every workflow step. Minutes to build.
- **`llvm`** — `FROM` the `base` digest. Builds LLVM/Clang `llvmorg-23.1.3` (the pin's newest point release;
  `ClangConfigVersion` matches on major.minor so 23.1.x is interchangeable) into `/opt/llvm-23` with:
  `LLVM_ENABLE_PROJECTS=clang`, `LLVM_TARGETS_TO_BUILD=X86`, `CMAKE_BUILD_TYPE=Release`,
  `LLVM_BUILD_LLVM_DYLIB=OFF`, `LLVM_LINK_LLVM_DYLIB=OFF`, `CLANG_LINK_CLANG_DYLIB=OFF`, `LLVM_ENABLE_RTTI=ON`,
  `LLVM_ENABLE_ZLIB=OFF`, `LLVM_ENABLE_ZSTD=OFF`, `LLVM_ENABLE_LIBXML2=OFF`, `LLVM_ENABLE_LIBEDIT=OFF`,
  `LLVM_ENABLE_TERMINFO=OFF`, `LLVM_ENABLE_FFI=OFF`, `LLVM_ENABLE_Z3_SOLVER=OFF`, `LLVM_INCLUDE_TESTS=OFF`,
  `LLVM_INCLUDE_EXAMPLES=OFF`, `LLVM_INCLUDE_BENCHMARKS=OFF`, `LLVM_INCLUDE_DOCS=OFF`, `CLANG_ENABLE_ARCMT=OFF`,
  `CLANG_ENABLE_STATIC_ANALYZER=OFF`, `LLVM_BUILD_TOOLS=OFF`, `CLANG_BUILD_TOOLS=OFF` (the libraries, the headers,
  the CMake package and the resource directory are all the front end needs; no `clang` driver binary),
  `LLVM_PARALLEL_LINK_JOBS=2`. The install is pruned to `lib/*.a`, `lib/cmake`, `include`, `lib/clang/23`.
- **Triggers:** push to `main` touching `infra/release/**` or the workflow itself, and `workflow_dispatch` with an
  input to force the `llvm` layer. `base` rebuilds on any change; `llvm` rebuilds only when the LLVM tag, the CMake
  flags, or `base`'s digest change. The job records the digests it produced in its summary, and the release workflow
  consumes a digest written into `infra/release/images.env`, so a toolchain change is a reviewed commit, not a moving
  tag.
- **Time budget.** An X86-only, tools-off Release build of clang+LLVM on 4 vCPUs is estimated at 2–3 hours; the
  6-hour limit leaves room, and the first run measures it. If it does not fit: `-DLLVM_OPTIMIZED_TABLEGEN=ON`,
  dropping `CLANG_ENABLE_OBJC_REWRITER`, or a two-job split with a `ccache` restored from the Actions cache. Record the
  measured wall time here.
- **Toolset source before 9.9 GA:** the AlmaLinux 9.9 **beta** AppStream repository, enabled by one line in the
  `base` Dockerfile and removed at GA ([toolset-source-before-ga](#toolset-source-before-ga), resolved).
- **First measurement, before any of the above is written:** whether the official LLVM release tarball's static
  archives can be linked on this image instead of building LLVM at all
  ([llvm-from-release-tarball](#llvm-from-release-tarball)). If they can, the `llvm` layer is a download and an
  unpack, and the time budget below is moot.

### [release-build-workflow](#release-build-workflow)

`.github/workflows/release-binary.yml`, running in `container: ghcr.io/steve-downey/specgen-toolchain@<llvm digest>`
(the package is public, so no registry credentials are needed), on push to `main`, on pull requests that touch the
release files, and on `workflow_dispatch`:

1. Checkout with `fetch-depth: 0` (tags for the version check) and `git config --global --add safe.directory`,
   which the coverage job already needs for the same reason (a root container and a checked-out tree).
2. Configure with a new preset `manylinux-release`: inherits `_root-config` and `_release-base`, plain `cc`/`c++`
   from the toolset (no toolchain file), `-DClang_DIR=/opt/llvm-23/lib/cmake/clang`,
   `-DBEMAN_SPECGEN_LLVM_LINK=static`, `-DBEMAN_SPECGEN_BUILD_EXAMPLES=OFF`, and `CMAKE_INSTALL_PREFIX` under a
   staging directory. The preset is usable locally by anyone who has the image (`docker run … cmake --preset
   manylinux-release`), which is how the workflow is debugged without pushing.
3. Build, then `ctest -E '^style\.'` — the full suite against the binary that ships, including every golden and the
   `.singlepass` agreements, minus the lint gate. Lint is a **pull-request check** (branch → `main`), where a finding
   can still change what lands; once a commit is on `main` it is too late for lint to matter, and this job's question
   is whether the binary works ([style-gate-in-release-job](#style-gate-in-release-job), resolved).
4. Install the `specgen_Runtime` component (binary and resource directory), add `LICENSE` and a short `README`, split
   the debug information out (`objcopy --only-keep-debug bin/specgen specgen.debug`), strip the binary fully
   (`objcopy --strip-unneeded --add-gnu-debuglink=specgen.debug bin/specgen`; the build-id note survives), and lay
   out `specgen-<version>-manylinux_2_34_x86_64/{bin/specgen,lib/clang/23/include/…,LICENSE,README.md}`. The
   `.debug` file is `xz`-compressed and travels as its **own release asset**, never inside the archive
   ([debug-info-shipping](#debug-info-shipping), resolved). The binary's size is a number to record from the first
   run; the expectation with only the front end's components linked, `--gc-sections` and a full strip is 35–50 MB,
   10–12 MB compressed.
5. **Verify** with `tools/release/check-portable.sh`, which fails the job if any of these is false: `ldd` lists only
   libraries in the `manylinux_2_34` whitelist; the highest `GLIBC_` version referenced is ≤ 2.34 and the highest
   `GLIBCXX_` is ≤ 3.4.29 (`objdump -T`); the staged binary, run from an empty directory, reports `--version` with
   the checkout's commit and parses a header that includes `<cstddef>` and `<optional>`.
6. **Build the wheel** (`packaging/pypi/`, see [pypi-wheel](#pypi-wheel)) and run `auditwheel show` on it, asserting
   the tag it computes is `manylinux_2_34_x86_64`. auditwheel is the ecosystem's authoritative reading of the policy
   and it is preinstalled in the PyPA image; using it from day one means the compatibility claim is never ours alone.
   The wheel is uploaded as an artifact and **not** published in this step.
7. Upload the archive, its `.sha256`, the `.debug` file, and the wheel as artifacts.
8. A `smoke` job, `needs: build`, downloads the archive and runs it inside a matrix of stock images (`ubuntu:22.04`,
   `ubuntu:24.04`, `debian:12`, `fedora:latest`, `almalinux:9`) with that distribution's `g++` installed: `--version`,
   `render --from-ir` on a checked-in IR, and `generate` on the `<optional>`-including header. This is the only place
   the "host libstdc++ headers" requirement is actually exercised across libstdc++ versions.
9. On every push to `main` (never on a pull request), a `release` job (`permissions: contents: write`,
   `id-token: write`, `attestations: write`) tags the commit `v<PROJECT_VERSION>-<run number>` and creates the GitHub
   Release with `gh release create` (the CLI is on every runner; no third-party action), attaching the archive,
   checksum and debug file, then runs `actions/attest-build-provenance` (GitHub's own, SHA-pinned like every action in
   this repository) over the assets. The archive's asset name carries no version
   (`specgen-manylinux_2_34_x86_64.tar.gz`; the version is inside it, in `--version`, and in the tag), so
   `https://github.com/steve-downey/specgen/releases/latest/download/specgen-manylinux_2_34_x86_64.tar.gz` is a
   stable, anonymous URL that always serves head ([release-trigger](#release-trigger), resolved).

The main CI's `build-and-test` matrix is untouched; this workflow is additive and its first lane's name is chosen once
and then left alone, for the branch-protection reason `ci_tests.yml` already documents.

### [docs-and-records](#docs-and-records)

- A decision record `docs/decisions/portable-release-binary.md`: static LLVM/Clang; gcc-toolset's nonshared shim in
  place of `-static-libstdc++`; `manylinux_2_34` as the floor and the reasons the two neighbours lost; the bundled
  resource directory and the host-libstdc++ runtime requirement; x86_64 first. Indexed in `docs/decisions/README.md`;
  [tool-not-library](../decisions/tool-not-library.md)'s "packaging apparatus awaits revisiting" consequence gains a
  pointer to it.
- `docs/building.md`: a "Release binary" section (the preset, the image, how to reproduce the workflow locally with
  `docker run`, the verify script). `README.md`: an "Install a prebuilt binary" paragraph ahead of "Quick Start", with
  the glibc floor, the x86-64-v2 baseline EL9 implies, and the host-g++ requirement. `user-guide.md` "Requirements":
  the same, in user terms.
- `AGENTS.md` test count, with `docs/building.md`, after [resource-dir-discovery](#resource-dir-discovery) adds its
  cases.

### [pypi-wheel](#pypi-wheel)

Publishing is last; the wheel itself exists from Step 4.

- `packaging/pypi/pyproject.toml`, project `beman-specgen` ([pypi-project-name](#pypi-project-name), resolved),
  version taken from the CMake `PROJECT_VERSION` at build
  time (one source of truth; the workflow fails if they disagree). The root `pyproject.toml` is the **development
  environment** `uv` manages and keeps that role; the wheel project is separate so `uv run cmake` never sees it.
- The wheel is `py3-none-manylinux_2_34_x86_64`: a package directory holding `bin/specgen` and `lib/clang/23/include`,
  and a console script `specgen` that `os.execv`s the binary, so the resource-directory rule from
  [resource-dir-discovery](#resource-dir-discovery) holds unchanged inside a site-packages tree.
- **Precedent, followed rather than reinvented.** The `cmake`, `ninja` and `clang-format` PyPI projects, two of which
  this repository already consumes through `uv` (`cmake==4.3.2`, `clang-format==22.1.5` in `pyproject.toml`), all
  have the same shape: build backend **scikit-build-core** with `wheel.py-api = "py3"` (one wheel per platform, no
  CPython ABI); a CMake `install()` that puts the tool under `<package>/data/bin/`; a console-script entry point in a
  tiny Python package that locates `data/bin/<tool>` beside itself and `os.execl`s it on POSIX (`subprocess` on
  Windows); the wheel's version equal to the tool's version; and **cibuildwheel** driving the build in the manylinux
  image, with `auditwheel repair` deciding the tag. `clang-format` is the closest relative: its `CMakeLists.txt`
  downloads the LLVM source for the release named by the version and builds one static target
  (`BUILD_SHARED_LIBS=OFF`, `LLVM_ENABLE_PROJECTS=clang`, `LLVM_TARGETS_TO_BUILD=` empty, `LLVM_ENABLE_ZLIB=OFF`,
  `LLVM_ENABLE_ZSTD=OFF`), strips it, and installs it to `clang_format/data/bin/`. `cmake` runs its build with
  `before-all` installing a compiler and a static OpenSSL into the image, which is the slot our toolchain
  (gcc-toolset-16 and the unpacked LLVM tarball) occupies.
- So `packaging/pypi/CMakeLists.txt` is a thin scikit-build-core project whose `install()` copies the staged tree from
  the workflow's build (binary, resource directory, license) into `beman_specgen/data/`, and
  `beman_specgen/__init__.py` is the `execl` shim. It does **not** rebuild specgen: the one build that ran the suite
  is the one that ships, and the wheel and the archive are two packagings of the same install tree. cibuildwheel is
  optional at that point; running
  `uv build --wheel` inside our own image and `auditwheel repair` by hand does the same with fewer moving parts, and
  cibuildwheel's `manylinux-x86_64-image` setting takes our `llvm` image by digest if the aarch64 lane later makes its
  matrix handling worth having.
- Publishing: PyPI **trusted publishing** (OIDC, no stored token) from the `release` job through
  `pypa/gh-action-pypi-publish`, SHA-pinned; a first manual upload to TestPyPI proves the project name and metadata.
  PyPI rejects local version labels and every version is immutable, so a release per `main` push cannot be mirrored
  there as-is: the job publishes **only when `PROJECT_VERSION` is not yet on PyPI**, so bumping the CMake version is
  what publishes, and the GitHub Release stream stays per-push.

### Later, and deliberately out of scope

- **aarch64**: the same two Dockerfiles on `quay.io/pypa/manylinux_2_34_aarch64`, built and run on the free
  `ubuntu-24.04-arm` runners (no emulation; an emulated LLVM build would not fit the time limit). Everything in Steps
  3–4 is written with the architecture as a matrix dimension so this is a one-line addition.
- **macOS**: not a manylinux question; a separate plan when there is demand.
- **A machine with no C++ standard library**: not supported; see [std-headers-at-run-time](#std-headers-at-run-time)
  for why bundling one would be wrong rather than merely deferred.

## Open questions

### [llvm-from-release-tarball](#llvm-from-release-tarball)

**Question:** can the `llvm` image layer unpack the official `LLVM-23.1.3-Linux-X64.tar.xz` instead of building LLVM
from source? **Status:** OPEN; the measurement is the first act of [toolchain-image](#toolchain-image). What is known
from the release workflow and its cache file (`release-binaries.yml`, `clang/cmake/caches/Release.cmake` on
`release/23.x`): the Linux binaries are built on **`ubuntu-22.04`** (glibc 2.35, one minor above the 2.34 floor),
two-stage with ThinLTO, PGO and BOLT, with `LLVM_ENABLE_FATLTO=ON` (so the archives' objects carry native code and
GNU `ld` can link them), with zstd and libxml2 **statically linked into LLVM but not shipped**, RTTI off (the front
end already follows that), and against Ubuntu's libstdc++ 12 headers. The questions the measurement answers, in
order: are the `libclang*.a` component archives in the tarball at all; do the objects reference any glibc symbol
newer than 2.34; which zstd/xml2/terminfo symbols are left undefined and whether EL9 can supply them statically; and
does the result pass the suite when linked by gcc-toolset-16 on the image. A no on any of them is the source build.

The first three were measured on 2026-10-12 against `LLVM-23.1.3-Linux-X64.tar.xz` (2.0 GB), and every one came out
in the tarball's favour:

- **315 static archives** are in it, among them `libclangTooling.a`, `libclangFormat.a`, `libclangFrontend.a`,
  `libclangAST.a` and `libLLVMSupport.a`, plus `lib/clang/23/include/` (the resource directory
  [resource-dir-discovery](#resource-dir-discovery) bundles) and `lib/cmake/{llvm,clang}`.
- **The glibc floor is exactly 2.34.** `libclang-cpp.so.23.1`, built from the same objects, references nothing newer
  than `GLIBC_2.34`; neither `libLLVMSupport.a` nor `libclangTooling.a` has an undefined reference to any symbol glibc
  added after 2.34 (`__memcmpeq`, `_Fork`, `arc4random*`, `__isoc23_*`, `pidfd_*` were checked). Being built on
  Ubuntu 22.04 did not move the floor above the policy's.
- **libstdc++ at `GLIBCXX_3.4.30`** (GCC 12), one notch above EL9's system `libstdc++.so.6` (`GLIBCXX_3.4.29`). That
  notch is precisely what gcc-toolset's `libstdc++_nonshared.a` supplies, so the link should resolve it statically;
  this is the one claim that only the fourth measurement, the link itself, can confirm.
- **Undefined externals:** `ZSTD_*` (six symbols, from `libLLVMSupport.a`); zlib arrives through `libz.so.1`, which
  the policy whitelists; libxml2 is confined to `libLLVMWindowsManifest.a`, which the front end never links; no
  terminfo, libedit, ffi or z3 (`LLVM_ENABLE_FFI OFF`, `LLVM_ENABLE_LIBEDIT FALSE`, `LLVM_WITH_Z3` empty in the
  shipped `LLVMConfig.cmake`). EL9 has `libzstd-devel` but **no `libzstd-static`** (CRB has `zlib-static` only), so
  the `base` layer builds zstd 1.5.x static from source, a one-minute step, rather than bundling a non-whitelisted
  `libzstd.so.1`.
- The archives' objects are ordinary ELF relocatables with native `.text` sections (`LLVM_ENABLE_FATLTO`), so
  gcc-toolset's GNU `ld` links them; no LLVM linker or LTO plugin is needed.
- `LLVM_ENABLE_RTTI OFF`, as the decision record already anticipates for the release binaries.

```sh
curl -sSL -o llvm.tar.xz https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.3/LLVM-23.1.3-Linux-X64.tar.xz
tar -tJf llvm.tar.xz | grep -c -E '/lib/lib(clang|LLVM)[A-Za-z0-9_]*\.a$'                       # 315
tar -xJf llvm.tar.xz --wildcards '*/lib/libclang-cpp.so.23.1' '*/lib/libclangTooling.a' '*/lib/libLLVMSupport.a'
objdump -T LLVM-23.1.3-Linux-X64/lib/libclang-cpp.so.23.1 | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1   # GLIBC_2.34
objdump -T LLVM-23.1.3-Linux-X64/lib/libclang-cpp.so.23.1 | grep -o 'GLIBCXX_[0-9.]*' | sort -Vu | tail -1 # 3.4.30
nm -u LLVM-23.1.3-Linux-X64/lib/libLLVMSupport.a | grep -o -E '\b(ZSTD_[A-Za-z]+|xml[A-Za-z]+|setupterm|el_init)\b' | sort -u
```

So the `llvm` layer is, until the link says otherwise, **download, verify the checksum, unpack, prune** (keep
`lib/*.a`, `lib/cmake`, `include`, `lib/clang/23`) — minutes, not hours. The source build in
[toolchain-image](#toolchain-image) stays written down as the fallback and as the route to an aarch64 lane, for which
the official `Linux-ARM64` tarball needs the same four checks.

The prune as written breaks `find_package(Clang)`: `LLVMExports-release.cmake` and `ClangTargets-release.cmake` check
that every imported file exists, and between them they name 105 tools in `bin/` and five shared libraries
(`libclang-cpp`, `libclang`, `libLTO`, `libRemarks`, `LLVMPolly`). Extracting all of them is gigabytes; the measurement
instead kept the real `bin/clang-format` (statically linked, and what the `.eastconst` goldens run) and created an
**empty, non-executable placeholder** for each of the rest, from the list that
`grep -ho '"${_IMPORT_PREFIX}/[^"]*"'` yields over those two files. Non-executable matters: `find_program` then
reports `clang-tidy` absent, which is what
[style-gate-in-release-job](#style-gate-in-release-job) assumes. A `clang-cpp` target still exists (it names the
placeholder), so the release preset sets `BEMAN_SPECGEN_LLVM_LINK=static` explicitly rather than trusting `auto`. The
tarball's provenance checks out with `gh attestation verify --repo llvm/llvm-project --bundle <tarball>.jsonl`; the
image pins the SHA-256 instead (`ca9fe385a5d9e2d88132dc0e23a00e9f87f38fa75f67207b5e457d208e71d013` for
23.1.3 x86_64).

No distribution packages LLVM 23 for EL9: AlmaLinux 9.8 AppStream carries 21.1.8 and EPEL 9's compat packages stop at
`llvm20`; `llvm-toolset` follows RHEL's rebase cadence, which puts 23 in 2027 at the earliest. Measured:

```sh
docker run --rm almalinux:9 dnf -q list --available llvm-devel clang-devel   # 21.1.8-2.el9
docker run --rm almalinux:9 sh -c 'dnf -q install -y epel-release; dnf -q list --available "llvm2*"'  # … llvm20
```

### [toolset-source-before-ga](#toolset-source-before-ga)

**Question:** where does `gcc-toolset-16` come from until AlmaLinux 9.9 is GA? **Status:** RESOLVED (2026-10-12,
decided by the user): (a). Three candidates were: (a) enable the AlmaLinux 9.9 **beta** AppStream repository in the
`base` Dockerfile and drop that line at GA, the packages being the same builds that go stable; (b) take the toolset
from CentOS Stream 9's AppStream, which has it today, at the cost of mixing a second distribution's repository into
the image; (c) wait, and build the images against `gcc-toolset-15` meanwhile knowing they cannot build the tree. (a)
keeps everything AlmaLinux and the diff to remove later is one `dnf config-manager` line. RHEL 9.9 (and so AlmaLinux
9.9) is expected in November 2026 on Red Hat's cadence; at GA the line goes.

### [style-gate-in-release-job](#style-gate-in-release-job)

**Question:** run `style.no-raw-loops` in the release job, or exclude it? **Status:** RESOLVED (2026-10-12, decided
by the user): exclude; lint is a branch-to-`main` pull-request check, and once a commit is on `main` lint is too
late. Running
it needs `clang-tools-extra` in the LLVM layer *and* a `clang-tidy` built to export its symbols for a plugin to
resolve against (a static `clang-tidy` does not, without `LLVM_EXPORT_SYMBOLS_FOR_PLUGINS`), for a check the main CI
already enforces on every PR. The release job's question is "does the shipped binary work", not "is the tree lint
clean". Excluding is spelled `-E '^style\.'` with a comment saying exactly this, so the "never a silent green" rule the
gate was built on is honoured by being loud in the workflow file instead.

### [release-trigger](#release-trigger)

**Question:** what does a push to `main` publish? **Status:** RESOLVED (2026-10-12, decided by the user): a GitHub
Release, every time. `main` is always releasable, "live at head" is the policy, and release branches or hand-cut
version tags can come much later. The candidates were: (a) An Actions artifact only:
simplest, but artifacts expire after 90 days and **downloading one requires a GitHub login**, which defeats "other
people". (b) A rolling pre-release named `edge`, re-cut from every `main` build: an anonymous, stable URL, at the cost
of a Release that is not a release and a changing asset behind a fixed name. (c) Both: artifacts on every build,
`edge` updated from `main`, and real Releases from `v*` tags. The ruling is stronger than any of them: each `main`
push *is* a release, tagged `v<PROJECT_VERSION>-<run number>`, with the unversioned asset name giving GitHub's
`releases/latest/download/` URL a stable target. The checksum and the provenance attestation make the stream
auditable.

### [pypi-project-name](#pypi-project-name)

**Question:** what is the distribution's name on PyPI? **Status:** RESOLVED (2026-10-12, decided by the user):
`beman-specgen`. Two names were in
play and only one is in question: the **command** stays `specgen` whatever the distribution is called, exactly as the
`clang-format` distribution installs a `clang-format`. Measured on 2026-10-12 against `pypi.org/pypi/<name>/json`:
`specgen` is **taken** (a 0.0.0 placeholder, "specify classes"), and `beman-specgen`, `cxx-specgen`, `cpp-specgen`,
`wg21-specgen`, `wg21-paper-specgen` and `specgen-wg21` are free. `c++-specgen` is not a legal name at all: PEP 508
allows ASCII letters, digits, `.`, `-` and `_` only, so the C++ spelling would be `cxx-` or `cpp-`. `beman-specgen`
won because it is the project's name already (`beman.specgen` in CMake, the Beman library convention
`beman.<name>`) and WG21 is the audience rather than the identity; `wg21-` prefixes read as the paper tooling the
tool feeds, not as the tool. The first upload to PyPI should happen early, since the placeholder squatting
`specgen` shows free names do not stay free. Consequence for the tree: the root
`pyproject.toml` is the development environment and is also named `beman-specgen`; it is renamed (for example
`beman-specgen-dev`) so the manifest nobody installs does not hold the name the wheel publishes under.

### [std-headers-at-run-time](#std-headers-at-run-time)

**Question:** should the archive carry a C++ standard library's headers so `generate` works on a machine with no
g++? **Status:** RESOLVED (2026-10-12, decided by the user): no. A header's wording must be generated against the
same standard library its project builds and tests with; a bundled libstdc++ would be a second one, and the paper
would then diverge from the implementation in a way no test can see. A paper that uses `<generator>` needs a host
library that has it in any case, and a machine with no standard library at all is not a case to support now. The
host's libstdc++ stays the source, found by Clang's GCC-installation detection, with `--gcc-install-dir=` in the tail
for a host that has several; the smoke matrix in [release-build-workflow](#release-build-workflow) exercises that path
on five distributions. Measured for the record: GCC 16's headers would have been 16 MB, 1.5 MB compressed, and would
still have needed the host's libc headers.

### [debug-info-shipping](#debug-info-shipping)

**Question:** ship `specgen.debug` beside the stripped binary? **Status:** RESOLVED (2026-10-12, decided by the user):
yes, as a separate release asset, and the binary in the archive is **fully stripped**, symbol table included, because
keeping the download tight is worth more than in-place symbolization; a stack trace of offsets plus the text the
tool was running on is enough to debug anything once the matching `.debug` is to hand. The Makefile already chooses
RelWithDebInfo for installs so that a failure in an installed binary is diagnosable; the separate asset keeps that
property for a stripped download, and it costs release storage, which GitHub does not meter for a public
repository, and nothing else. Measured on the `gcc-release` driver, 2026-10-12: specgen's own text and data
are 2 MB and its DWARF 93 MB, which xz takes to 13 MB; the LLVM release archives carry no DWARF at all, so the
`.debug` file covers exactly our frames. The binary already has a GNU build-id, so `--add-gnu-debuglink` lets gdb
and `llvm-symbolizer` match it automatically. Mechanically: `objcopy --only-keep-debug` first, then `objcopy
--strip-unneeded --add-gnu-debuglink` on the binary, `xz` the debug file, and `gh release upload` both; `gdb -ex
'symbol-file specgen.debug'` on a core or
`llvm-symbolizer --obj=specgen.debug <offset>` on a trace line resolves it. A static LLVM and Clang carry on the
order of a hundred thousand long mangled names, so the symbol table alone would be tens of megabytes, which is the
size argument against keeping it.

```sh
readelf -SW specgen | awk '/\.debug_/{d+=strtonum("0x"$6)} END{print d/1e6 " MB"}'   # 93
objcopy --only-keep-debug specgen specgen.debug && xz -k specgen.debug && ls -l specgen.debug.xz   # 13 MB
```
