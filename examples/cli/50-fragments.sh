#!/bin/sh
# examples/cli/50-fragments.sh                                        -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# `--split` writes one file per top-level stable-name section and prints a
# manifest in document order. Each backend is run from inside its own output
# directory with a *relative* `--split wording`, so the manifest holds
# `wording/optional.ctor.tex` rather than an absolute path that would differ on
# every machine -- the same thing the split-mode golden does, and what lets
# these files be compared against it.
. "$(dirname -- "$0")/env.sh"

OUT=$(out_dir 50-fragments)
IR=$REPO_ROOT/tests/golden/optional/expected.json

for backend in latex mpark; do
    mkdir -p "$OUT/$backend"
    (
        cd "$OUT/$backend"
        "$SPECGEN" render --from-ir "$IR" \
            --backend "$backend" \
            --split wording > manifest
    )
done

# The org split names its root fragment explicitly; without `--root` the name
# is derived from the longest shared prefix of the section names.
mkdir -p "$OUT/org"
(
    cd "$OUT/org"
    "$SPECGEN" render --from-ir "$IR" \
        --backend org \
        --root optional.syn \
        --split wording > manifest
)
