# tests/tidy/run-strip.cmake                                      -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The negative test from the plan (docs/plans/no-raw-loops-tidy-plugin.md,
# "Testing the check"): break every marker in a copy of a production file and
# require exactly that many findings — one finding per stripped marker, since
# the house rule is that each marked loop carries its own marker.
#
#   cmake -DCLANG_TIDY=... -DPLUGIN=... -DSOURCE=<production.cpp>
#         -DINCLUDE_DIR=<repo include/> -DIMPLICIT_DIRS=<dir|dir|...>
#         -DSCRATCH=<copy path> -P run-strip.cmake
#
# IMPLICIT_DIRS is the configured C++ compiler's implicit include search
# path, |-separated (a ;-list would not survive the test command line). It is
# handed to clang-tidy as explicit -isystem directories with -nostdinc++, so
# the parse uses exactly the standard library the build uses, independent of
# clang's own GCC-installation detection — which is what makes this test give
# one answer on every box the tree builds on.

file(READ "${SOURCE}" _text)
string(REGEX MATCHALL "substrate generic algorithm" _markers "${_text}")
list(LENGTH _markers _marker_count)
if(_marker_count EQUAL 0)
    message(FATAL_ERROR "${SOURCE} carries no markers; the test pins nothing")
endif()
string(
    REPLACE "substrate generic algorithm"
    "redacted generic algorithm"
    _text
    "${_text}"
)
file(WRITE "${SCRATCH}" "${_text}")

string(REPLACE "|" ";" _implicit_dirs "${IMPLICIT_DIRS}")
set(_args
    -std=c++2c
    -nostdinc
    -nostdinc++
    -I
    "${INCLUDE_DIR}"
)
foreach(_dir IN LISTS _implicit_dirs)
    list(APPEND _args -isystem "${_dir}")
endforeach()

execute_process(
    COMMAND
        ${CLANG_TIDY} --load ${PLUGIN} --checks=-*,specgen-no-raw-loops
        ${SCRATCH} -- ${_args}
    OUTPUT_VARIABLE raw_out
    ERROR_VARIABLE raw_err
    RESULT_VARIABLE status
)
if(NOT status EQUAL 0)
    message(
        FATAL_ERROR
        "clang-tidy failed on ${SCRATCH} (exit ${status}):\n${raw_out}\n${raw_err}"
    )
endif()

# Count by the bracketed check tag alone: the diagnostic's message text
# contains a semicolon, and a matched string with a `;` in it splits into two
# elements the moment it lands in a CMake list, doubling a warning-line
# count. The tag appears exactly once per finding and carries no `;`.
string(REGEX MATCHALL "\\[specgen-no-raw-loops\\]" _findings "${raw_out}")
list(LENGTH _findings _finding_count)
if(NOT _finding_count EQUAL _marker_count)
    message(
        FATAL_ERROR
        "stripped ${_marker_count} markers from ${SOURCE} but the check reported ${_finding_count} findings:\n${raw_out}"
    )
endif()
