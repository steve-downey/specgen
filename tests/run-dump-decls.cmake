# tests/run-dump-decls.cmake                                     -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# dump-decls's two refusals (issue #144). A header that cannot be read is not a
# partial parse, so it exits 1 with generate's message rather than 0 with
# nothing printed, which read as an empty header. Several headers are a usage
# error, exit 2, rather than the last one silently winning.

if(NOT DEFINED DRIVER OR NOT DEFINED HEADER OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "DRIVER, HEADER, and WORK_DIR are required")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

execute_process(
    COMMAND "${DRIVER}" dump-decls does-not-exist.hpp --no-compile-commands
    WORKING_DIRECTORY "${WORK_DIR}"
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    RESULT_VARIABLE result
)
if(NOT result EQUAL 1)
    message(
        FATAL_ERROR
        "dump-decls on a missing header exited ${result}, not 1:\n${stderr}"
    )
endif()
if(NOT stdout STREQUAL "")
    message(FATAL_ERROR "dump-decls on a missing header printed:\n${stdout}")
endif()
if(
    NOT stderr
        MATCHES
        "specgen: cannot read 'does-not-exist.hpp', or Clang could not parse it"
)
    message(FATAL_ERROR "unexpected message for a missing header:\n${stderr}")
endif()

execute_process(
    COMMAND "${DRIVER}" dump-decls "${HEADER}" "${HEADER}" --no-compile-commands
    WORKING_DIRECTORY "${WORK_DIR}"
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    RESULT_VARIABLE result
)
if(NOT result EQUAL 2)
    message(
        FATAL_ERROR
        "dump-decls on two headers exited ${result}, not 2:\n${stderr}"
    )
endif()
if(NOT stderr MATCHES "takes one header, not 2")
    message(FATAL_ERROR "unexpected message for two headers:\n${stderr}")
endif()
