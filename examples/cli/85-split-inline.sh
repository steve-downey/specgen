#!/bin/sh
# examples/cli/85-split-inline.sh                                     -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# `split_inline` turns a header with in-class member definitions into the
# shape specgen reads: the class body declaration-only, the definitions after
# the class inside its own namespace block. Two conversions: a header already
# in specgen form, whose `//!` docblocks travel with the definitions under
# `--move-comments`; and a plain class template, whose definitions take the
# template heads, qualifier and `inline` an out-of-line definition needs.
#
# Both outputs equal the expected files of the tool's own cases
# (tests/split_inline/cases), which is what the examples.parity cases check.
. "$(dirname -- "$0")/env.sh"

if [ ! -x "$SPLIT_INLINE" ]; then
    printf 'split_inline not found at %s\n' "$SPLIT_INLINE" >&2
    printf 'Run `make install-release`, or set SPLIT_INLINE to a build-tree binary.\n' >&2
    exit 1
fi

OUT=$(out_dir 85-split-inline)
cd "$REPO_ROOT"

# A specgen-form header with in-class definitions: the docblocks move to the
# definitions, under the `\rSec3` section their `\ref` group names.
"$SPLIT_INLINE" tests/corpus/spec_inclass.hpp \
    --no-compile-commands --move-comments \
    -o "$OUT/inclass.hpp"

# A constrained class template, converted without touching its comments.
"$SPLIT_INLINE" tests/split_inline/cases/members/input.hpp \
    --no-compile-commands \
    -o "$OUT/members.hpp"
