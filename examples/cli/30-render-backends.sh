#!/bin/sh
# examples/cli/30-render-backends.sh                                  -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# One hand-written IR fixture through all three backends. The input is
# identical in each case and only the serializer changes, which is what makes
# the three outputs worth reading side by side. Needs no Clang: `render` is
# clang-free, so this script runs on every lane.
. "$(dirname -- "$0")/env.sh"

OUT=$(out_dir 30-render-backends)
cd "$REPO_ROOT"

# Draft LaTeX is the default backend; named here so the command reads the same
# as the other two.
"$SPECGEN" render --from-ir tests/golden/value_or/input.json \
    --backend latex \
    -o "$OUT/value_or.tex"

"$SPECGEN" render --from-ir tests/golden/value_or/input.json \
    --backend mpark \
    -o "$OUT/value_or.md"

# The long spelling of -o, which is the same option.
"$SPECGEN" render --from-ir tests/golden/value_or/input.json \
    --backend org \
    --output "$OUT/value_or.org"
