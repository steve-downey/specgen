# tests/run-version.cmake                                        -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if(
    NOT DEFINED DRIVER
    OR NOT DEFINED EXPECTED_VERSION
    OR NOT DEFINED EXPECTED_COMMIT
)
    message(
        FATAL_ERROR
        "DRIVER, EXPECTED_VERSION, and EXPECTED_COMMIT are required"
    )
endif()

execute_process(
    COMMAND "${DRIVER}" --version
    OUTPUT_VARIABLE actual
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_VARIABLE stderr
    RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "specgen --version exited ${result}: ${stderr}")
endif()

set(expected "specgen ${EXPECTED_VERSION} (git ${EXPECTED_COMMIT})")
if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "expected '${expected}', got '${actual}'")
endif()
