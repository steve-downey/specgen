#!/bin/sh
# examples/cli/45-wording-diff.sh                                     -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The wording diff: a header rendered against a baseline, the wording coming
# out marked with what the revision added and removed. The corpus pair is the
# same header one revision apart; `--baseline-header` parses the old one.
. "$(dirname -- "$0")/env.sh"

OUT=$(out_dir 45-wording-diff)
cd "$REPO_ROOT"

"$SPECGEN" generate tests/corpus/diff/gadget.after.hpp \
    --baseline-header tests/corpus/diff/gadget.before.hpp \
    --backend mpark \
    --no-compile-commands \
    -o "$OUT/gadget-diff.md"

"$SPECGEN" generate tests/corpus/diff/gadget.after.hpp \
    --baseline-header tests/corpus/diff/gadget.before.hpp \
    --backend org \
    --no-compile-commands \
    -o "$OUT/gadget-diff.org"

# The `items` context elides the members that did not change.
"$SPECGEN" generate tests/corpus/diff/gadget.after.hpp \
    --baseline-header tests/corpus/diff/gadget.before.hpp \
    --diff-context items \
    --backend mpark \
    --no-compile-commands \
    -o "$OUT/gadget-diff-items.md"
