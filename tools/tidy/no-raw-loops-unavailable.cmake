# tools/tidy/no-raw-loops-unavailable.cmake                        -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The loud-fail half of the no-raw-loops gate
# (docs/plans/no-raw-loops-tidy-plugin.md, ctest-gate): when the configure
# probe found no clang-tidy plugin toolchain, style.no-raw-loops is
# registered to run this script instead of the pass, so the gate's absence
# is a visible test failure rather than a silent green.
message(
    FATAL_ERROR
    "style.no-raw-loops cannot run: the configure-time probe found no "
    "clang-tidy plugin toolchain for the pinned LLVM (the 'beman.specgen: "
    "clang-tidy plugin toolchain' STATUS line names the missing piece; "
    "clang-tidy/ClangTidyCheck.h ships in clang-tools-extra's development "
    "package). The no-raw-loops doctrine is not being enforced until it is "
    "installed."
)
