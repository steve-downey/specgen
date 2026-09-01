#!/bin/sh
# examples/cli/20-widget-ir.sh                                        -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The smallest useful header, spec_widget.hpp, to IR -- and the same IR piped
# straight into a renderer. `--no-compile-commands` is not decoration: the
# repository keeps a gitignored compile_commands.json symlink at its root and
# `generate` probes for one by default, so without this the parse would depend
# on whether a build happens to be configured. It is also exactly what the
# golden harness passes, which is what lets widget.json be compared against
# golden.widget_skeleton's expected.json byte for byte.
. "$(dirname -- "$0")/env.sh"
require_tier_b

OUT=$(out_dir 20-widget-ir)
cd "$REPO_ROOT"

"$SPECGEN" generate --emit-ir tests/corpus/spec_widget.hpp \
    --no-compile-commands \
    -o "$OUT/widget.json"

# Generate and render in one pass, without an intermediate file.
"$SPECGEN" generate --emit-ir tests/corpus/spec_widget.hpp --no-compile-commands |
    "$SPECGEN" render --from-ir - --backend latex \
        -o "$OUT/widget-piped.tex"
