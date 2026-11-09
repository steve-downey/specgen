# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""The `specgen` and `split_inline` commands, installed from the beman-specgen wheel.

The wheel carries the native executables under data/bin and Clang's builtin
headers under data/lib/clang, the layout both expect beside themselves. This
module only finds an executable and replaces the Python process with it.
"""

import os
import sys
from pathlib import Path

DATA = Path(__file__).resolve().parent / "data"


def executable(name: str = "specgen") -> Path:
    """The path of the native executable `name` this wheel installed."""
    return DATA / "bin" / name


def run(name: str) -> None:
    program = str(executable(name))
    os.execv(program, [program, *sys.argv[1:]])


def main() -> None:
    """The `specgen` command."""
    run("specgen")


def split_inline() -> None:
    """The `split_inline` command: in-class definitions to out-of-line form."""
    run("split_inline")
