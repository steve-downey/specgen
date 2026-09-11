# tests/tidy/run-check.cmake                                      -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Run one probe source through the pinned clang-tidy with the specgen plugin
# loaded, keep only the specgen-no-raw-loops finding lines with the probe's
# directory stripped, and compare byte-for-byte against the checked-in
# expected file — the golden pattern, for diagnostics.
#
#   cmake -DCLANG_TIDY=... -DPLUGIN=... -DSOURCE=<probe.cpp>
#         -DEXPECTED=<probe.expected> -DACTUAL=<out> -P run-check.cmake
#
# The probes are freestanding (no includes), so no standard-library search
# path is needed; a probe that fails to *parse* fails the test loudly, since
# a diagnostic from a broken TU pins nothing.

execute_process(
    COMMAND
        ${CLANG_TIDY} --load ${PLUGIN} --checks=-*,specgen-no-raw-loops
        ${SOURCE} -- -std=c++2c
    OUTPUT_VARIABLE raw_out
    ERROR_VARIABLE raw_err
    RESULT_VARIABLE status
)
if(NOT status EQUAL 0)
    message(
        FATAL_ERROR
        "clang-tidy failed on ${SOURCE} (exit ${status}):\n${raw_out}\n${raw_err}"
    )
endif()

get_filename_component(_source_dir "${SOURCE}" DIRECTORY)
string(REPLACE ";" "\\;" _lines "${raw_out}")
string(REPLACE "\n" ";" _lines "${_lines}")
set(_findings "")
foreach(_line IN LISTS _lines)
    if(_line MATCHES "warning:.*\\[specgen-no-raw-loops\\]")
        string(REPLACE "${_source_dir}/" "" _line "${_line}")
        string(APPEND _findings "${_line}\n")
    endif()
endforeach()

file(WRITE "${ACTUAL}" "${_findings}")

file(READ "${EXPECTED}" _expected)
if(NOT _findings STREQUAL _expected)
    message(
        FATAL_ERROR
        "findings differ from ${EXPECTED}\n--- expected ---\n${_expected}--- actual (${ACTUAL}) ---\n${_findings}"
    )
endif()
