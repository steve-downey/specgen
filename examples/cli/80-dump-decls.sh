#!/bin/sh
# examples/cli/80-dump-decls.sh                                       -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# `dump-decls` prints the declaration/comment event stream the front end builds
# the document from. Useful when a section is empty and should not be.
. "$(dirname -- "$0")/env.sh"
require_tier_b

OUT=$(out_dir 80-dump-decls)
cd "$REPO_ROOT"

"$SPECGEN" dump-decls tests/corpus/spec_widget.hpp \
    --no-compile-commands > "$OUT/widget-decls.txt"
