# packaging/release/base.Dockerfile                         -*-dockerfile-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The `base` layer of the release toolchain image
# (docs/plans/manylinux-release-binary.md, toolchain-image): PyPA's
# manylinux_2_34 image, which is AlmaLinux 9 with glibc 2.34, plus GCC 16 from
# Red Hat's gcc-toolset, ninja, and a static zstd. The `llvm` layer
# (llvm.Dockerfile) builds on this one.
#
# gcc-toolset links libstdc++_nonshared.a into every binary, so what GCC 16's
# libstdc++ has and EL9's system libstdc++.so.6 lacks travels inside the
# executable, which then needs no newer GLIBCXX_ version than EL9 has. That is
# why the compiler comes from the distribution and nothing here says
# -static-libstdc++.

# Pinned by digest, never by a moving tag; created 2026-10-09.
FROM quay.io/pypa/manylinux_2_34_x86_64@sha256:3cb61cd981757b4ed54bc658f8bd0f4e829af49372cf8cf6c237268dacf168d0

# gcc-toolset-16 is in AlmaLinux 9.9, which is still in beta; until 9.9 is GA
# it comes from the beta AppStream repository, restricted to the toolset's own
# packages so nothing else from the beta can reach the image
# (toolset-source-before-ga). At GA this file goes and the plain AppStream
# repository serves the same packages.
COPY almalinux-9.9-beta.repo /etc/yum.repos.d/

# GCC 16 links -latomic by default, and gcc-c++ does not pull in the
# toolset's libatomic archive.
RUN dnf install -y --setopt=install_weak_deps=False \
        gcc-toolset-16-gcc-c++ \
        gcc-toolset-16-libatomic-devel \
        ninja-build \
    && dnf clean all

# The image's own environment points at gcc-toolset-14 and puts /opt/clang/bin
# first; the toolset's `enable` script cannot be sourced from ENV, so its
# effect is spelled out, and every later layer and every workflow step sees
# GCC 16 as cc/c++.
ENV PATH=/opt/rh/gcc-toolset-16/root/usr/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    LD_LIBRARY_PATH=/opt/rh/gcc-toolset-16/root/usr/lib64:/opt/rh/gcc-toolset-16/root/usr/lib \
    DEVTOOLSET_ROOTPATH=/opt/rh/gcc-toolset-16/root \
    CC=gcc \
    CXX=g++

# LLVM's Support library references zstd, and the manylinux policy does not
# allow libzstd.so.1, so it has to be linked statically. EL9 ships no
# libzstd-static (CRB has zlib-static only), so build the archive from the
# release source, position-independent so it links into a PIE driver.
ARG ZSTD_VERSION=1.5.7
ARG ZSTD_SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
RUN set -eux; \
    cd /tmp; \
    curl -sSfL -o zstd.tar.gz \
        "https://github.com/facebook/zstd/releases/download/v${ZSTD_VERSION}/zstd-${ZSTD_VERSION}.tar.gz"; \
    echo "${ZSTD_SHA256}  zstd.tar.gz" | sha256sum -c -; \
    tar -xzf zstd.tar.gz; \
    make -C "zstd-${ZSTD_VERSION}/lib" -j"$(nproc)" CFLAGS="-O2 -fPIC" libzstd.a; \
    make -C "zstd-${ZSTD_VERSION}/lib" PREFIX=/usr/local install-static install-includes; \
    rm -rf /tmp/zstd.tar.gz "/tmp/zstd-${ZSTD_VERSION}"

# A checkout owned by someone else is the normal case in a CI container.
RUN git config --system --add safe.directory '*'
