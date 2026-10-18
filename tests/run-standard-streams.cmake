# tests/run-standard-streams.cmake                               -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# `-` on either side of a render. `--from-ir -` is standard input, which names
# no file, so a dependency fragment leaves it out rather than listing `-` as a
# prerequisite with an empty rule for make to go looking for (issue #145).
# `-o -` is standard output: the wording is printed, no file named `-` is
# written, and the two options that need a real output file -- --depfile
# without --dep-target, and --split -- are usage errors beside it.

if(NOT DEFINED DRIVER OR NOT DEFINED IR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "DRIVER, IR, and WORK_DIR are required")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

# --from-ir - with --depfile: the rule names the output and nothing else.
execute_process(
    COMMAND "${DRIVER}" render --from-ir - --depfile wording.d -o wording.tex
    WORKING_DIRECTORY "${WORK_DIR}"
    INPUT_FILE "${IR}"
    ERROR_VARIABLE stderr
    RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "render --from-ir - exited ${result}:\n${stderr}")
endif()
file(READ "${WORK_DIR}/wording.d" depfile)
if(NOT depfile STREQUAL "wording.tex:\n")
    message(
        FATAL_ERROR
        "standard input reached the dependency fragment:\n${depfile}"
    )
endif()

# -o -: the wording on standard output, and no file named `-`.
execute_process(
    COMMAND "${DRIVER}" render --from-ir "${IR}" -o -
    WORKING_DIRECTORY "${WORK_DIR}"
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "render -o - exited ${result}:\n${stderr}")
endif()
if(NOT stdout MATCHES "begin\\{codeblock\\}")
    message(FATAL_ERROR "render -o - printed no wording:\n${stdout}")
endif()
if(EXISTS "${WORK_DIR}/-")
    message(FATAL_ERROR "render -o - wrote a file named '-'")
endif()

# The usage errors: each exits 2 and names the conflict.
function(expect_usage_error pattern)
    execute_process(
        COMMAND "${DRIVER}" render --from-ir "${IR}" -o - ${ARGN}
        WORKING_DIRECTORY "${WORK_DIR}"
        OUTPUT_QUIET
        ERROR_VARIABLE stderr
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 2)
        message(
            FATAL_ERROR
            "render -o - ${ARGN} exited ${result}, not 2:\n${stderr}"
        )
    endif()
    if(NOT stderr MATCHES "${pattern}")
        message(
            FATAL_ERROR
            "render -o - ${ARGN}: unexpected message:\n${stderr}"
        )
    endif()
endfunction()
expect_usage_error("so --depfile needs --dep-target" --depfile wording.d)
expect_usage_error("would both write to standard output" --split fragments)
