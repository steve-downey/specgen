# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""The `specgen` command, installed from the beman-specgen wheel.

The wheel carries the native driver under data/bin and Clang's builtin headers
under data/lib/clang, the layout the driver expects beside itself. This module
only finds the driver and replaces the Python process with it.
"""

import os
import sys
from pathlib import Path

DATA = Path(__file__).resolve().parent / "data"


def executable() -> Path:
    """The path of the native specgen driver this wheel installed."""
    return DATA / "bin" / "specgen"


def main() -> None:
    driver = str(executable())
    os.execv(driver, [driver, *sys.argv[1:]])
