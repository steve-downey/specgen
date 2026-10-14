#!/bin/sh
# tools/release/stage.sh                                            -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# usage: stage.sh <build dir> <out dir>
#
# Turn a built manylinux-release tree into the release's assets, in <out dir>:
#
#   specgen-<version>-manylinux_2_34_x86_64/      the staged tree, kept for
#       bin/specgen                               check-portable.sh
#       lib/clang/<major>/include/...
#       LICENSE  README.md
#   specgen-manylinux_2_34_x86_64.tar.gz          that tree, archived
#   specgen-manylinux_2_34_x86_64.tar.gz.sha256
#   specgen.debug.xz                              the driver's debug information
#
# The archive's name carries no version, so GitHub's releases/latest/download/
# URL for it is stable; the version is the directory inside, `--version`, and
# the release tag (release-trigger). The binary is stripped of everything,
# symbol table included, and its debug information travels as its own asset,
# never in the archive (debug-info-shipping); the build-id and a debuglink
# naming specgen.debug let gdb and llvm-symbolizer pair them.
#
# Under GitHub Actions, the version and the staged tree's path are also
# written to $GITHUB_OUTPUT as `version` and `stage`.
set -eu

build=$1
out=$2
source_dir=$(cd "$(dirname "$0")/../.." && pwd)

version=$(sed -n 's/^CMAKE_PROJECT_VERSION:[A-Z]*=//p' "$build/CMakeCache.txt")
[ -n "$version" ] || {
    echo "stage: no CMAKE_PROJECT_VERSION in $build/CMakeCache.txt" >&2
    exit 1
}
name=specgen-$version-manylinux_2_34_x86_64
archive=specgen-manylinux_2_34_x86_64.tar.gz
stage=$out/$name

rm -rf "$stage" "$out/$archive" "$out/$archive.sha256" "$out/specgen.debug" "$out/specgen.debug.xz"
mkdir -p "$out"
cmake --install "$build" --component specgen_Runtime --prefix "$stage"
cp "$source_dir/LICENSE" "$stage/"
cp "$source_dir/packaging/release/README.md" "$stage/"

objcopy --only-keep-debug "$stage/bin/specgen" "$out/specgen.debug"
objcopy --strip-all --add-gnu-debuglink="$out/specgen.debug" "$stage/bin/specgen"
xz -T0 "$out/specgen.debug"

# Owner and order fixed, so the archive's bytes depend on its contents alone.
tar -C "$out" --sort=name --owner=0 --group=0 --numeric-owner -cf - "$name" |
    gzip -9n >"$out/$archive"
(cd "$out" && sha256sum "$archive" >"$archive.sha256")

ls -l "$stage/bin/specgen" "$out/$archive" "$out/specgen.debug.xz"
if [ -n "${GITHUB_OUTPUT:-}" ]; then
    echo "version=$version" >>"$GITHUB_OUTPUT"
    echo "stage=$stage" >>"$GITHUB_OUTPUT"
fi
