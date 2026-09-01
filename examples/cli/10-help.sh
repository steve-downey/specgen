#!/bin/sh
# examples/cli/10-help.sh                                             -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# General command information.
. "$(dirname -- "$0")/env.sh"

OUT=$(out_dir 10-help)
cd "$REPO_ROOT"

"$SPECGEN" --help > "$OUT/specgen-help.txt"
"$SPECGEN" generate --help > "$OUT/generate-help.txt"
"$SPECGEN" render --help > "$OUT/render-help.txt"
"$SPECGEN" dump-decls --help > "$OUT/dump-decls-help.txt"
