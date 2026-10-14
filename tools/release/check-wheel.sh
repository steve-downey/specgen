#!/bin/sh
# tools/release/check-wheel.sh                                      -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# usage: check-wheel.sh <wheel> [<expected git commit> [<clang argument>...]]
#
# Install the wheel into a fresh virtual environment and run smoke.sh on the
# `specgen` console script it installs: the Python shim must find the driver,
# and the driver must find Clang's builtin headers inside site-packages.
set -eu

wheel=$(readlink -f "$1")
shift

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

uv venv -q "$work/venv"
uv pip install -q --python "$work/venv/bin/python" "$wheel"
"$(dirname "$0")/smoke.sh" "$work/venv/bin/specgen" "$@"
