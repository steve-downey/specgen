# A portable release binary for manylinux_2_34

## Status

Accepted

## Context

`specgen` is a tool, not a library ([tool-not-library](tool-not-library.md)),
so the thing to ship is the executable. Building it needs GCC 16 on C++26
([cxx26-baseline](cxx26-baseline.md)) and exactly LLVM 23.1
([llvm-toolchain-pin](llvm-toolchain-pin.md)), which few machines have; a
build against a distribution's LLVM also links `libclang-cpp.so` and
`libLLVM.so`, and runs only where that distribution's packages are. Someone
who wants to generate wording should be able to download one archive and run
it.

A downloadable Linux binary is portable to the systems whose glibc and
libstdc++ are at least as new as the ones it was linked against, and to no
others. The Python packaging ecosystem's `manylinux` policies (PEP 600) name
those floors, publish build images for them, and come with `auditwheel`, an
independent reading of whether a binary meets one. The candidates were
`manylinux_2_28` (glibc 2.28, AlmaLinux 8), `manylinux_2_34` (glibc 2.34,
AlmaLinux 9) and `manylinux_2_39` (glibc 2.39, AlmaLinux 10). The plan,
`docs/plans/manylinux-release-binary.md`, holds the measurements behind each
point below.

## Decision

**The release binary is `manylinux_2_34_x86_64`**: glibc 2.34 or newer, which
is Ubuntu 22.04, Debian 12, Fedora 35, RHEL 9 and their derivatives, or later.
`manylinux_2_28` lost because AlmaLinux 8's newest GCC toolset is 14, older
than the floor. `manylinux_2_39` lost because PyPA publishes no x86_64 image
for it, and because EL10's x86-64-v3 baseline would put AVX2 code into the
statically linked parts of the binary.

**It is built in an image of our own**, `ghcr.io/steve-downey/specgen-toolchain`:
PyPA's `manylinux_2_34_x86_64` image plus Red Hat's `gcc-toolset-16` for the
compiler, a static zstd, and the official LLVM 23.1 release's static archives
unpacked to `/opt/llvm`. The release build names the image by digest
(`packaging/release/images.env`), so a toolchain change is a reviewed commit.

**libstdc++ is linked the gcc-toolset way, not statically.** A gcc-toolset
links `libstdc++_nonshared.a` into every binary, so what GCC 16's libstdc++
has and EL9's system `libstdc++.so.6` lacks travels inside the executable,
and the executable asks for nothing newer than `GLIBCXX_3.4.29`. There is no
`-static-libstdc++`.

**Clang and LLVM are linked statically**
(`-DBEMAN_SPECGEN_LLVM_LINK=static`), so the driver loads no LLVM library;
it needs only `libz`, `libgcc_s`, `libstdc++`, `libm` and `libc`.

**The archive carries Clang's resource directory and no C++ standard
library.** Clang's builtin headers sit in `lib/clang/<major>/include` beside
`bin/specgen`, where the driver finds them from its own location. The C++
standard library comes from the host's GCC installation, because a paper's
wording should be generated against the standard library its implementation
builds with; a bundled one would be a second library, and wording from it
could diverge from the implementation without any test noticing. A host whose
only GCC is a gcc-toolset under `/opt/rh` names it with `--gcc-install-dir=`,
since Clang's detection does not look there.

**Every push to `main` is a release.** The release workflow builds and tests
the binary, checks it against the policy (`tools/release/check-portable.sh`),
runs it on five stock distributions, and publishes a GitHub Release tagged
`v<version>-<run>` whose asset names carry no version, so
`releases/latest/download/` always serves head. The binary is fully stripped;
its debug information is a separate asset, `specgen.debug.xz`.

**x86_64 first.** aarch64 is the same image on PyPA's aarch64 base and a free
arm64 runner, once there is demand.

## Consequences

- Nobody needs GCC 16 or LLVM 23 to run `specgen`; they need glibc 2.34, an
  x86-64-v2 processor, and their own `g++` for any header that includes the
  standard library.
- The toolchain is pinned three times over: PyPA's image by digest, the
  gcc-toolset by its AlmaLinux repository, and LLVM by version and SHA-256.
  Moving any of them is a change to `packaging/release/` followed by a change
  to `images.env`.
- `gcc-toolset-16` comes from the AlmaLinux 9.9 beta repository until 9.9 is
  generally available; then `packaging/release/almalinux-9.9-beta.repo` goes.
- The release job does not run the `style.` lint gate; lint is a pull-request
  check, and the image has no `clang-tidy`.
- The PyPI wheel, `beman-specgen`, packages the same staged tree a second
  way, and `auditwheel` checks this claim independently on every build. Its
  instruction-set check is the exception: everything linked on EL9 needs
  x86-64-v2, which the tag cannot express, so that requirement is documented
  and held at v2 by `check-portable.sh` instead.
