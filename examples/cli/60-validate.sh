#!/bin/sh
# examples/cli/60-validate.sh                                         -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Validators run in the normal render path. Clang-free, like every other
# render-mode script.
. "$(dirname -- "$0")/env.sh"

OUT=$(out_dir 60-validate)
cd "$REPO_ROOT"

# A document that validates clean still renders.
"$SPECGEN" render --from-ir tests/golden/value_or/input.json \
    --validate \
    --backend latex \
    -o "$OUT/value_or-validated.tex"

# A finding at Error severity is reported on stderr, rendering is skipped, and
# the exit status is 1. That non-zero exit is this example's expected outcome
# rather than a failure, so it is caught here instead of tripping `set -e`.
if "$SPECGEN" render --from-ir tests/golden/validate_coverage/input.json \
    --validate \
    --backend latex \
    -o "$OUT/coverage.tex" 2> "$OUT/coverage.diag"; then
    printf 'expected a validation failure from validate_coverage, got success\n' >&2
    exit 1
fi
