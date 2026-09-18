#!/bin/sh
# examples/cli/55-paper.sh                                            -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# A paper of several headers in one invocation. `generate` takes one header per
# document and renders them as one paper -- validated across the union of their
# documented names, so a name specified by a sibling document is not foreign --
# writing the assembled clause with `-o`, the per-clause fragments with
# `--split`, and a Makefile dependency fragment with `--depfile`.
#
# The headers are copied into the output directory and everything runs from
# there with relative paths, for the reason 50-fragments.sh runs from inside its
# own: a manifest or a dependency fragment holding an absolute path would differ
# on every machine. It also makes the capture read like the project it is an
# example for -- headers under include/, wording written beside them.
. "$(dirname -- "$0")/env.sh"

require_tier_b

OUT=$(out_dir 55-paper)
CORPUS=$REPO_ROOT/tests/corpus

mkdir -p "$OUT/include/support"
cp "$CORPUS/spec_paper_main.hpp" "$CORPUS/spec_paper_sibling.hpp" "$OUT/include/"
cp "$CORPUS/support/spec_paper_traits.hpp" "$OUT/include/support/"

cd "$OUT"

# One parse of both headers, three outputs: the whole, the pieces, and the
# dependency fragment that says what the first two were built from.
"$SPECGEN" generate include/spec_paper_main.hpp include/spec_paper_sibling.hpp \
    --validate \
    --no-compile-commands \
    -o expected.tex \
    --split wording \
    --root paper.mix.syn --root paper.blend.syn \
    --depfile wording.d > manifest
