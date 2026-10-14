#!/bin/sh
# tools/release/smoke.sh                                            -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# usage: smoke.sh <specgen> [<expected git commit> [<clang argument>...]]
#
# Does a staged or downloaded specgen work on this machine? Run from an empty
# directory, so nothing in the checkout or the build tree can stand in for
# what the archive carries, it must:
#
#   - report --version, naming the expected commit when one is given;
#   - generate wording from tools/release/smoke/smoke.hpp, which includes the
#     host's <cstddef> and <optional>, without a word on stderr;
#   - produce the same bytes through --emit-ir and render --from-ir as in one
#     pass, the agreement every generate-mode golden's .singlepass pins.
#
# Clang arguments after the commit (an empty commit skips that check) go to
# both parses, after `--`. The build image needs one: its only C++ standard
# library is gcc-toolset's, under /opt/rh, which Clang's GCC detection does not
# search, so it is named with --gcc-install-dir= exactly as a user with only a
# toolset installed would name it.
#
# The release workflow runs this in the build image (through check-portable.sh)
# and again on each distribution of its smoke matrix, where the host's
# libstdc++ is a different version from the one the binary was built with.
set -eu

specgen=$(readlink -f "$1")
expected_commit=${2:-}
shift
[ $# -eq 0 ] || shift
header=$(readlink -f "$(dirname "$0")/smoke/smoke.hpp")

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"

fail() {
    echo "smoke: $*" >&2
    exit 1
}

version=$("$specgen" --version) || fail "--version exited $?"
echo "$version"
case "$version" in
    "specgen "*" (git "*")") ;;
    *) fail "unexpected --version output: $version" ;;
esac
if [ -n "$expected_commit" ]; then
    case "$version" in
        *"(git $expected_commit)") ;;
        *) fail "--version does not name commit $expected_commit" ;;
    esac
fi

"$specgen" generate "$header" --no-compile-commands -- "$@" >single.tex 2>single.err ||
    fail "generate exited $?: $(cat single.err)"
[ ! -s single.err ] || fail "generate wrote to stderr: $(cat single.err)"
grep -q 'optional<size_t> index() const noexcept;' single.tex ||
    fail "generate's output lacks the declaration: $(cat single.tex)"

"$specgen" generate --emit-ir "$header" --no-compile-commands -- "$@" >smoke.ir ||
    fail "generate --emit-ir exited $?"
"$specgen" render --from-ir smoke.ir >two.tex || fail "render --from-ir exited $?"
cmp -s single.tex two.tex || fail "generate and render --from-ir disagree: $(diff single.tex two.tex)"

echo "smoke: ok"
