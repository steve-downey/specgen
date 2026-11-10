# tests/split_inline/run-case.cmake                                -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Run one split_inline case (docs/plans/split-inline.md, "Checking a
# conversion"), in one of these MODEs:
#   convert      run TOOL on INPUT, compare its standard output with EXPECTED
#                byte for byte and its standard error with EXPECTED_STDERR when
#                that is set (a refusal case), and require exit status
#                EXPECTED_STATUS (0 by default). Run from INPUT's own directory
#                with the file's bare name, so a refusal names `input.hpp:12`
#                and not a build path.
#   fixed_point  run TOOL on EXPECTED and require its output to be EXPECTED
#                again: the conversion is idempotent.
#   same_wording run SPECGEN over INPUT and over EXPECTED and require the two
#                renderings to agree: moving a definition out of line, with
#                its docblock, changes no wording.
# OPTIONS holds the tool's own options (`--move-comments`, ...), as a list.
# Every run passes `--no-compile-commands`, for the reason tests/golden does:
# a gitignored compile_commands.json at the repository root must not change
# what a test parses.

foreach(var MODE INPUT EXPECTED ACTUAL)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "run-case: ${var} is required")
    endif()
endforeach()
if(NOT DEFINED EXPECTED_STATUS)
    set(EXPECTED_STATUS 0)
endif()

get_filename_component(input_dir "${INPUT}" DIRECTORY)
get_filename_component(input_name "${INPUT}" NAME)
get_filename_component(actual_dir "${ACTUAL}" DIRECTORY)
file(MAKE_DIRECTORY "${actual_dir}")

function(require_same what a b)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E compare_files "${a}" "${b}"
        RESULT_VARIABLE differ
    )
    if(differ)
        execute_process(
            COMMAND diff -u "${a}" "${b}"
            OUTPUT_VARIABLE delta
            ERROR_VARIABLE delta
        )
        message(FATAL_ERROR "${what} differs from ${a}:\n${delta}")
    endif()
endfunction()

if(MODE STREQUAL "convert")
    execute_process(
        COMMAND "${TOOL}" "${input_name}" --no-compile-commands ${OPTIONS}
        WORKING_DIRECTORY "${input_dir}"
        OUTPUT_FILE "${ACTUAL}"
        ERROR_FILE "${ACTUAL}.stderr"
        RESULT_VARIABLE status
    )
    if(NOT status EQUAL EXPECTED_STATUS)
        file(READ "${ACTUAL}.stderr" stderr)
        message(
            FATAL_ERROR
            "split_inline exited ${status}, not ${EXPECTED_STATUS}:\n${stderr}"
        )
    endif()
    require_same("the converted header" "${EXPECTED}" "${ACTUAL}")
    if(DEFINED EXPECTED_STDERR)
        require_same("the diagnostics" "${EXPECTED_STDERR}" "${ACTUAL}.stderr")
    endif()
elseif(MODE STREQUAL "fixed_point")
    get_filename_component(expected_dir "${EXPECTED}" DIRECTORY)
    get_filename_component(expected_name "${EXPECTED}" NAME)
    execute_process(
        COMMAND "${TOOL}" "${expected_name}" --no-compile-commands ${OPTIONS}
        WORKING_DIRECTORY "${expected_dir}"
        OUTPUT_FILE "${ACTUAL}"
        ERROR_VARIABLE stderr
        RESULT_VARIABLE status
    )
    # A refusal case refuses the same members the second time round, so its
    # status is the same non-zero one; what must not change is the text.
    if(NOT status EQUAL EXPECTED_STATUS)
        message(
            FATAL_ERROR
            "split_inline over its own output exited ${status}, not ${EXPECTED_STATUS}:\n${stderr}"
        )
    endif()
    require_same("the second conversion" "${EXPECTED}" "${ACTUAL}")
elseif(MODE STREQUAL "same_wording")
    if(NOT DEFINED SPECGEN)
        message(FATAL_ERROR "run-case: same_wording needs SPECGEN")
    endif()
    foreach(side INPUT EXPECTED)
        execute_process(
            COMMAND
                "${SPECGEN}" generate "${${side}}" --no-compile-commands
                --backend latex -o "${ACTUAL}.${side}.tex"
            ERROR_VARIABLE stderr
            RESULT_VARIABLE status
        )
        if(NOT status EQUAL 0)
            message(
                FATAL_ERROR
                "specgen generate over ${${side}} exited ${status}:\n${stderr}"
            )
        endif()
    endforeach()
    require_same("the converted header's wording" "${ACTUAL}.INPUT.tex" "${ACTUAL}.EXPECTED.tex")
else()
    message(FATAL_ERROR "run-case: unknown MODE '${MODE}'")
endif()
