#!/bin/sh
# examples/cli/40-paper-mode.sh                                       -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Paper mode is mpark-only: it wraps the fragment in an editing-instruction div
# and numbers added paragraphs x, x+1, and so on. No other backend has it.
. "$(dirname -- "$0")/env.sh"

OUT=$(out_dir 40-paper-mode)
cd "$REPO_ROOT"

"$SPECGEN" render --from-ir tests/golden/paper_mode/input.json \
    --backend mpark \
    --paper \
    -o "$OUT/paper-mode.md"
