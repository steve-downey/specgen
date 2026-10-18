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
