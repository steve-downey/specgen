#!/bin/sh
# docs/optional-ref-monadic/generate.sh                               -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Generate the wording for [optional.ref.monadic] from the header beside this
# script, in each backend, and the same wording marked against the working
# draft's own text for that subclause. Everything under output/ is what these
# commands wrote.
#
# The driver is $SPECGEN, or the binary `make install-release` installs.
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
SPECGEN=${SPECGEN:-$REPO_ROOT/.install/bin/specgen}
HEADER=docs/optional-ref-monadic/optional_ref_monadic.hpp
OUT=docs/optional-ref-monadic/output

cd "$REPO_ROOT"
rm -rf "$OUT"
mkdir -p "$OUT/wording" "$OUT/diff"

# The draft's text is the slice checked in here, staged as the file
# --baseline-draft looks for.
DRAFT=$(mktemp -d)
trap 'rm -rf "$DRAFT"' EXIT
cp docs/optional-ref-monadic/optional.ref.monadic.slice "$DRAFT/utilities.tex"

for backend in latex mpark org; do
    # The wording: the class synopsis is the root fragment, and the subclause
    # is a fragment of its own.
    "$SPECGEN" generate "$HEADER" --backend "$backend" --validate \
        --no-compile-commands \
        --split "$OUT/wording/$backend" --root optional.ref.synopsis \
        >"$OUT/wording/$backend.manifest" 2>>"$OUT/validate.diag"

    # The same subclause marked against the draft. The synopsis has no
    # subclause of the draft to be compared with, so only the subclause's
    # fragment is kept.
    "$SPECGEN" generate "$HEADER" --backend "$backend" \
        --baseline-draft "$DRAFT" \
        --no-compile-commands \
        --split "$OUT/diff/$backend" --root optional.ref.synopsis \
        >/dev/null 2>"$OUT/diff/$backend.diag"
    rm -f "$OUT/diff/$backend"/optional.ref.synopsis.*
done

"$SPECGEN" --version >"$OUT/specgen-version.txt"
