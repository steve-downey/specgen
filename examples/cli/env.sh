# examples/cli/env.sh                                                 -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Sourced by every capture script. Resolves the driver and the output
# directory, and nothing else -- each script owns its own commands so that the
# text a reader sees in docs/examples.org is the text that ran.

set -eu

# Repository root, derived from this file rather than from $PWD, so a script
# can be run from anywhere while still invoking specgen with the repository
# relative paths the golden harness uses.
CLI_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$CLI_DIR/../.." && pwd)

# The driver: an explicit $SPECGEN wins, otherwise the installed binary at the
# location `make install-release` writes. A build-tree binary is
# reached by setting SPECGEN, which is what the ctest rerun cases do.
SPECGEN=${SPECGEN:-$REPO_ROOT/.install/bin/specgen}
if [ ! -x "$SPECGEN" ]; then
    printf 'specgen not found at %s\n' "$SPECGEN" >&2
    printf 'Run `make install-release`, or set SPECGEN to a build-tree binary.\n' >&2
    exit 1
fi

# Where captures land. The rerun tests point this at a scratch directory and
# diff the result against the checked-in tree; a plain run rewrites the
# checked-in tree in place.
OUT_ROOT=${EXAMPLES_OUT:-$CLI_DIR/output}

# Create and echo this script's own output directory.
out_dir() {
    _d=$OUT_ROOT/$1
    rm -rf -- "$_d"
    mkdir -p -- "$_d"
    printf '%s\n' "$_d"
}

# Every specgen build has the Clang front end, so `generate` and
# `dump-decls` are always available and there is no tier to check. What is
# still worth catching is a stale installed binary built without the front
# end, whose clang-free stubs ignore their arguments and print "this build has
# no Clang front end" -- silently, as far as a capture is concerned, since the
# message goes to stderr and the script would capture an empty file. This
# check is cheap insurance against exactly that.
require_tier_b() {
    if "$SPECGEN" generate --emit-ir "$REPO_ROOT/tests/corpus/spec_widget.hpp" \
        --no-compile-commands -o /dev/null 2>&1 |
        grep -q 'no Clang front end'; then
        printf '%s needs a specgen with the Clang front end; %s was built without one.\n' "$0" "$SPECGEN" >&2
        printf 'Rebuild and re-install with `make install-release`.\n' >&2
        exit 1
    fi
}
