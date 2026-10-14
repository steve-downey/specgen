#!/bin/sh
# packaging/release/stub-llvm-exports.sh                          -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# usage: stub-llvm-exports.sh <llvm prefix>
#
# A pruned LLVM install fails find_package(Clang): the exported target files
# check that every file they import exists, and between them they name a
# hundred-odd tools, a handful of shared libraries, and archives for projects
# (MLIR, BOLT, Polly) the front end never links. Create an empty file for each
# one that is missing, so the package loads and only what was kept is real.
#
# The placeholders are deliberately not executable: find_program skips them,
# so a probe for clang-tidy comes up empty rather than finding a zero-byte
# file. Linking one by mistake fails loudly, as a malformed archive.
set -eu

prefix=$1
cd "$prefix"

grep -ho '"${_IMPORT_PREFIX}/[^"]*"' \
    lib/cmake/llvm/LLVMExports-*.cmake \
    lib/cmake/clang/ClangTargets-*.cmake |
    sed -e 's/^"//' -e 's/"$//' |
    tr ';' '\n' |
    sed -e 's|^${_IMPORT_PREFIX}/||' |
    sort -u |
    while read -r file; do
        if [ ! -e "$file" ]; then
            mkdir -p "$(dirname "$file")"
            : >"$file"
            chmod 0644 "$file"
        fi
    done
