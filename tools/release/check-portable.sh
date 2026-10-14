#!/bin/sh
# tools/release/check-portable.sh                                   -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# usage: check-portable.sh <staged dir> [<expected git commit>]
#
# Is the staged release tree (stage.sh's output: bin/specgen beside
# lib/clang/<major>/include) what the manylinux_2_34_x86_64 name promises?
# Fails unless every one of these holds:
#
#   - the driver needs no shared library outside the manylinux_2_34 policy's
#     whitelist, less its X11, GL and GLib entries, which a command-line tool
#     has no business needing;
#   - it references no symbol version newer than EL9's own: GLIBC_2.34,
#     GLIBCXX_3.4.29 and CXXABI_1.3.13 (gcc-toolset's libstdc++_nonshared.a
#     carries anything newer inside the binary);
#   - it works from an empty directory (smoke.sh).
#
# auditwheel is the policy's authoritative reading and the wheel gets it; this
# is the same claim made for the archive, which no PyPA tool looks at.
set -eu

stage=$1
expected_commit=${2:-}
specgen=$stage/bin/specgen
status=0

fail() {
    echo "check-portable: $*" >&2
    status=1
}

[ -x "$specgen" ] || {
    echo "check-portable: no executable at $specgen" >&2
    exit 1
}
ls -d "$stage"/lib/clang/*/include/stddef.h >/dev/null 2>&1 ||
    fail "no Clang resource directory under $stage/lib/clang"

allowed='libgcc_s.so.1 libstdc++.so.6 libm.so.6 libdl.so.2 librt.so.1 libc.so.6
libnsl.so.1 libutil.so.1 libpthread.so.0 libresolv.so.2 libz.so.1 libexpat.so.1
ld-linux-x86-64.so.2'
needed=$(readelf -d "$specgen" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
echo "needed: $(echo $needed)"
for lib in $needed; do
    echo "$allowed" | tr ' ' '\n' | grep -qxF "$lib" ||
        fail "needs $lib, which manylinux_2_34 does not allow"
done

# newest <prefix>: the highest <prefix>_x.y.z version the dynamic symbol table
# references, or nothing.
newest() {
    objdump -T "$specgen" | grep -o "$1_[0-9][0-9.]*" | sed "s/^$1_//" | sort -Vu | tail -1
}
# check_version <prefix> <ceiling>
check_version() {
    v=$(newest "$1")
    echo "newest $1: ${v:-none} (ceiling $2)"
    if [ -n "$v" ] && [ "$(printf '%s\n%s\n' "$v" "$2" | sort -V | tail -1)" != "$2" ]; then
        fail "references $1_$v, newer than $1_$2"
    fi
}
check_version GLIBC 2.34
check_version GLIBCXX 3.4.29
check_version CXXABI 1.3.13

"$(dirname "$0")/smoke.sh" "$specgen" "$expected_commit" || status=1

[ "$status" -eq 0 ] && echo "check-portable: ok"
exit "$status"
