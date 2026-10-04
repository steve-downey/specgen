#!/bin/sh
# examples/cli/40-paper-mode.sh                                       -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Paper mode marks a complete mpark or org fragment as added wording and
# numbers its paragraphs x, x+1, and so on.
. "$(dirname -- "$0")/env.sh"

OUT=$(out_dir 40-paper-mode)
cd "$REPO_ROOT"

"$SPECGEN" render --from-ir tests/golden/paper_mode/input.json \
    --backend mpark \
    --paper \
    -o "$OUT/paper-mode.md"

"$SPECGEN" render --from-ir tests/golden/paper_mode/input.json \
    --backend org \
    --paper \
    -o "$OUT/paper-mode.org"
