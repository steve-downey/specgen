#!/bin/sh
# examples/cli/run-all.sh                                             -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The whole capture procedure: run every script in document order, rewriting
# the checked-in output tree. This is the one command to run on a machine with
# a Clang-enabled build after changing anything a captured file depends on.
#
#   make install-release       # or: export SPECGEN=<build tree>/specgen
#   examples/cli/run-all.sh
#
# The scripts that need the Clang front end say so and stop; the rest run on
# any build, which is why this script does not check for it up front.
set -eu

CLI_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

for script in "$CLI_DIR"/[0-9][0-9]-*.sh; do
    printf '=== %s\n' "$(basename -- "$script")"
    "$script"
done
