#!/bin/sh
# examples/cli/47-wording-diff-draft.sh                               -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The wording diff against the C++ working draft itself: no earlier revision of
# the header, just the draft's LaTeX. `--baseline-draft` names the draft's
# source directory and reads, by stable name, the sections the header
# generates. Here the "draft" is the importer's corpus slice of
# [optional.assign], staged as the `.tex` file the flag looks for.
. "$(dirname -- "$0")/env.sh"

OUT=$(out_dir 47-wording-diff-draft)
cd "$REPO_ROOT"

DRAFT=$(mktemp -d)
trap 'rm -rf "$DRAFT"' EXIT
cp tests/corpus/standard-slices/optional.assign.slice "$DRAFT/utilities.tex"

"$SPECGEN" generate tests/corpus/diff/optional_assign.hpp \
    --baseline-draft "$DRAFT" \
    --backend mpark \
    --no-compile-commands \
    -o "$OUT/optional-assign-diff.md"
