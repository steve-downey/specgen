# packaging/release/llvm.Dockerfile                         -*-dockerfile-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The `llvm` layer of the release toolchain image
# (docs/plans/manylinux-release-binary.md, toolchain-image): the `base` layer
# plus the official LLVM release's static Clang and LLVM archives at
# /opt/llvm, which is what `-DBEMAN_SPECGEN_LLVM_LINK=static` links
# against. A download and an unpack, not a build: the release tarball's
# objects need nothing newer than glibc 2.34 (llvm-from-release-tarball).

# The base layer, by digest, as the toolchain-image workflow passes it.
ARG BASE_IMAGE
FROM ${BASE_IMAGE}

# The pin's newest point release; ClangConfigVersion matches on major.minor, so
# any 23.1.x satisfies BEMAN_SPECGEN_LLVM_VERSION=23.1 (decision
# llvm-toolchain-pin). The checksum is of the x86_64 tarball, whose GitHub
# attestation was verified when it was recorded here.
ARG LLVM_VERSION=23.1.3
ARG LLVM_SHA256=ca9fe385a5d9e2d88132dc0e23a00e9f87f38fa75f67207b5e457d208e71d013

COPY stub-llvm-exports.sh /usr/local/libexec/

# Keep only what the front end compiles and links against: the LLVM and Clang
# archives, their headers and CMake packages, the builtin headers the driver
# bundles (lib/clang/<major>/include), and clang-format, which the
# `.eastconst` goldens run and which the release tarball links statically.
# Everything else the CMake packages name becomes a placeholder.
RUN set -eux; \
    major="${LLVM_VERSION%%.*}"; \
    name="LLVM-${LLVM_VERSION}-Linux-X64"; \
    curl -sSfL -o /tmp/llvm.tar.xz \
        "https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/${name}.tar.xz"; \
    echo "${LLVM_SHA256}  /tmp/llvm.tar.xz" | sha256sum -c -; \
    mkdir -p /opt/llvm; \
    tar -xJf /tmp/llvm.tar.xz -C /opt/llvm --strip-components=1 --wildcards \
        "${name}/lib/libLLVM*.a" \
        "${name}/lib/libclang*.a" \
        "${name}/lib/cmake/llvm/*" \
        "${name}/lib/cmake/clang/*" \
        "${name}/include/llvm/*" \
        "${name}/include/llvm-c/*" \
        "${name}/include/clang/*" \
        "${name}/include/clang-c/*" \
        "${name}/lib/clang/${major}/include/*" \
        "${name}/bin/clang-format"; \
    rm /tmp/llvm.tar.xz; \
    sh /usr/local/libexec/stub-llvm-exports.sh /opt/llvm; \
    /opt/llvm/bin/clang-format --version

ENV BEMAN_SPECGEN_CLANG_DIR=/opt/llvm/lib/cmake/clang
