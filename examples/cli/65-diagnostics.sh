#!/bin/sh
# examples/cli/65-diagnostics.sh                                      -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# `generate` reports source and docblock findings while still emitting IR for
# the header it parsed. Run from the header's own directory, and naming the
# header by file name alone, because the findings carry the path specgen was
# given -- an absolute path here would make the captured text machine-specific.
# That is exactly how the diagnose-mode golden runs, which is what lets this
# be compared against golden.diagnostics' expected.diag.
. "$(dirname -- "$0")/env.sh"
require_tier_b

OUT=$(out_dir 65-diagnostics)

(
    cd "$REPO_ROOT/tests/corpus"
    "$SPECGEN" generate --emit-ir spec_diagnostics.hpp \
        --no-compile-commands \
        -o "$OUT/diagnostics.json" 2> "$OUT/diagnostics.diag"
)
