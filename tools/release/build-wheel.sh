#!/bin/sh
# tools/release/build-wheel.sh                                      -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# usage: build-wheel.sh <staged dir> <out dir>
#
# Package stage.sh's tree as the beman-specgen wheel (packaging/pypi) and let
# auditwheel tag it: `auditwheel repair --plat manylinux_2_34_x86_64` refuses
# a wheel whose ELF files need more than that policy allows, so a wheel that
# comes out of this script under that tag is the policy's own verdict on the
# binary, not ours. Writes exactly one .whl into <out dir>.
#
# Needs uv and auditwheel, both of which the PyPA image the toolchain is built
# on provides.
set -eu

stage=$(cd "$1" && pwd)
out=$2
source_dir=$(cd "$(dirname "$0")/../.." && pwd)
plat=manylinux_2_34_x86_64

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The ISA check is off because the policy has no way to say what is true: the
# driver needs x86-64-v2, as everything linked on EL9 does (its crt1.o carries
# the marking, and its compilers default to -march=x86-64-v2), and a
# manylinux_2_34_x86_64 tag means baseline x86-64. The requirement is
# documented instead, and check-portable.sh holds the line at v2.
uv build --wheel "$source_dir/packaging/pypi" -o "$work/raw" \
    -C cmake.define.BEMAN_SPECGEN_STAGE="$stage"
mkdir -p "$out"
auditwheel repair --disable-isa-ext-check --plat "$plat" -w "$out" "$work"/raw/*.whl

wheel=$(ls "$out"/*-py3-none-*.whl)
case "$wheel" in
    *"-py3-none-$plat.whl") ;;
    *) echo "build-wheel: expected a $plat wheel, got $wheel" >&2; exit 1 ;;
esac
auditwheel show --disable-isa-ext-check "$wheel"
echo "build-wheel: $wheel"
