#!/bin/sh
# tools/release/check-version.sh                                    -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# usage: check-version.sh [<base revision>]
#
# Every push to main is a release, and the PyPI wheel publishes only a
# PROJECT_VERSION its index does not have yet, so a change that lands on main
# without a new version ships as a GitHub Release that PyPI never sees. This
# checks, in the working tree, that:
#
#   - the version in pyproject.toml, vcpkg.json and the user guide's
#     `specgen --version` example is CMakeLists.txt's PROJECT_VERSION; and
#   - given a base revision, PROJECT_VERSION is greater there than in that
#     revision's CMakeLists.txt.
#
# Whether the bump is a patch or a minor one is the author's call; this only
# checks that there is one.
set -eu

cd "$(dirname "$0")/../.."

# PROJECT_VERSION is the VERSION line inside project( ... ).
project_version() {
    sed -n '/^project($/,/^)$/s/^ *VERSION \([0-9][0-9.]*\)$/\1/p'
}

version=$(project_version < CMakeLists.txt)
if [ -z "$version" ]; then
    echo "check-version: no VERSION in CMakeLists.txt's project()" >&2
    exit 1
fi

status=0
agrees() {
    file=$1
    found=$2
    if [ "$found" != "$version" ]; then
        echo "check-version: $file says '${found}', CMakeLists.txt says $version" >&2
        status=1
    fi
}
agrees pyproject.toml "$(sed -n 's/^version = "\(.*\)"$/\1/p' pyproject.toml)"
agrees vcpkg.json "$(sed -n 's/^ *"version-semver": "\(.*\)",$/\1/p' vcpkg.json)"
agrees docs/user-guide.md "$(sed -n 's/.*`specgen \([0-9][0-9.]*\) (git <commit>)`.*/\1/p' docs/user-guide.md)"

if [ $# -gt 0 ]; then
    base=$(git show "$1:CMakeLists.txt" | project_version)
    newest=$(printf '%s\n%s\n' "$base" "$version" | sort -V | tail -n 1)
    if [ "$version" = "$base" ] || [ "$newest" != "$version" ]; then
        echo "check-version: PROJECT_VERSION $version is not greater than $base on $1;" \
             "bump it (patch or minor) in CMakeLists.txt, pyproject.toml," \
             "vcpkg.json and docs/user-guide.md" >&2
        status=1
    else
        echo "check-version: $base -> $version"
    fi
fi

exit $status
