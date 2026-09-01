#!/bin/sh
# examples/cli/90-optional.sh                                         -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The acid-test-shaped corpus header, end to end: header to IR, then that
# generated IR to mpark/wg21 markdown. The rendering is deliberately taken from
# the IR this script just generated rather than from the checked-in fixture, so
# the pair of captured files stands as one pipeline rather than two unrelated
# runs -- and both halves still match their goldens, which is what says the
# generated IR and the checked-in IR are the same bytes.
. "$(dirname -- "$0")/env.sh"
require_tier_b

OUT=$(out_dir 90-optional)
cd "$REPO_ROOT"

"$SPECGEN" generate --emit-ir tests/corpus/spec_optional.hpp \
    --no-compile-commands \
    --output "$OUT/optional.json"

"$SPECGEN" render --from-ir "$OUT/optional.json" \
    --backend mpark \
    -o "$OUT/optional.md"
