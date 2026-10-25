# tests/run-resource-dir.cmake                                   -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# The driver hands Clang the resource directory it carries beside itself
# (frontend/parse_header.cpp, bundled_resource_dir_args): run `generate -- -v` and read the
# `-resource-dir` back out of Clang's own -cc1 line. It has to be absolute,
# since a relative one resolves against whatever directory the user is in, and
# it has to hold Clang's builtin headers. Run from an empty directory, so a
# relative path that only works from the build tree cannot pass by accident.

if(NOT DEFINED DRIVER OR NOT DEFINED HEADER OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "DRIVER, HEADER, and WORK_DIR are required")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
execute_process(
    COMMAND
        "${DRIVER}" generate "${HEADER}" --no-compile-commands -- -nostdlibinc
        -v
    WORKING_DIRECTORY "${WORK_DIR}"
    OUTPUT_QUIET
    ERROR_VARIABLE stderr
    RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "specgen generate exited ${result}:\n${stderr}")
endif()

if(NOT stderr MATCHES "\"-resource-dir\" \"([^\"]*)\"")
    message(FATAL_ERROR "no -resource-dir in Clang's -cc1 line:\n${stderr}")
endif()
set(resource_dir "${CMAKE_MATCH_1}")
if(NOT IS_ABSOLUTE "${resource_dir}")
    message(FATAL_ERROR "-resource-dir '${resource_dir}' is not absolute")
endif()
if(NOT EXISTS "${resource_dir}/include/stddef.h")
    message(
        FATAL_ERROR
        "-resource-dir '${resource_dir}' holds no include/stddef.h"
    )
endif()
