#!/bin/sh
# examples/cli/70-include-path.sh                                     -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The two ways compiler arguments reach the front end, over one header that
# needs an include directory. Both must produce the same IR -- that is the
# point of running them side by side, and both captures are compared against
# the *same* checked-in golden.
#
# The compilation database is written to a temporary directory and thrown away
# rather than captured: its JSON bakes absolute paths, so it would differ on
# every machine. Only the IR it produces is kept, which carries no paths at all.
. "$(dirname -- "$0")/env.sh"
require_tier_b

OUT=$(out_dir 70-include-path)
cd "$REPO_ROOT"

# Arguments after the first bare `--` go straight to Clang.
"$SPECGEN" generate --emit-ir \
    tests/corpus/include_path/consumer/spec_include.hpp \
    --no-compile-commands \
    -o "$OUT/include-path.json" \
    -- -I tests/corpus/include_path

DB=$(mktemp -d)
trap 'rm -rf -- "$DB"' EXIT

HEADER=$REPO_ROOT/tests/corpus/include_path/consumer/spec_include.hpp
printf '[{"directory":"%s","file":"%s","arguments":["c++","-I","%s","-c","-o","spec_include.o","%s"]}]\n' \
    "$DB" "$HEADER" "$REPO_ROOT/tests/corpus/include_path" "$HEADER" \
    > "$DB/compile_commands.json"

"$SPECGEN" generate --emit-ir \
    tests/corpus/include_path/consumer/spec_include.hpp \
    --compile-commands "$DB" \
    -o "$OUT/include-path-from-db.json"
