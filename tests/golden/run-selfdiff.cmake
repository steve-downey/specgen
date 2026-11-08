# tests/golden/run-selfdiff.cmake                                  -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The wording diff's identity law at the command line
# (docs/plans/wording-diff.md): a document diffed against itself under
# `--diff-context all` renders the same bytes as the document rendered plain.
# IR is checked-in IR JSON; both renders go to scratch files beside ACTUAL.

if(NOT SPECGEN OR NOT IR OR NOT ACTUAL)
    message(
        FATAL_ERROR
        "run-selfdiff.cmake: SPECGEN, IR, and ACTUAL are required"
    )
endif()

execute_process(
    COMMAND "${SPECGEN}" render --from-ir "${IR}" -o "${ACTUAL}.plain"
    RESULT_VARIABLE plain_result
    ERROR_VARIABLE plain_error
)
if(NOT plain_result EQUAL 0)
    message(
        FATAL_ERROR
        "rendering the IR plain failed (exit ${plain_result}):\n${plain_error}"
    )
endif()
execute_process(
    COMMAND
        "${SPECGEN}" render --from-ir "${IR}" --baseline-ir "${IR}"
        --diff-context all -o "${ACTUAL}.selfdiff"
    RESULT_VARIABLE self_result
    ERROR_VARIABLE self_error
)
if(NOT self_result EQUAL 0)
    message(
        FATAL_ERROR
        "rendering the IR against itself failed (exit ${self_result}):\n${self_error}"
    )
endif()
execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E compare_files "${ACTUAL}.plain"
        "${ACTUAL}.selfdiff"
    RESULT_VARIABLE compare_result
)
if(NOT compare_result EQUAL 0)
    message(
        FATAL_ERROR
        "a document diffed against itself rendered differently from the plain render: "
        "${ACTUAL}.plain vs ${ACTUAL}.selfdiff"
    )
endif()
