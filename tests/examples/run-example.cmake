# tests/examples/run-example.cmake                                 -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Checks a captured example, in one of two MODEs. Together they are what makes
# docs/examples.org's output trustworthy without anyone having to take the
# document's word for it.
#
#   rerun   Run the capture script again, into a scratch directory, with this
#           build's driver, and compare everything it wrote against the
#           checked-in capture. This is the check for output that has no golden
#           twin -- the --help texts, the dump-decls stream, the piped render.
#
#   tree    Compare a checked-in captured directory against a checked-in golden
#           directory, file set first and then contents. Runs no binary at all,
#           so it works on every lane; used for the fragment captures, whose
#           golden twin is a directory rather than a file.
#
# Single-file parity needs neither mode: those cases are a plain
# `cmake -E compare_files` registered directly, comparing one captured file
# against the golden the harness already pins to real tool output. That
# indirection is the whole trick -- the golden suite proves golden == what
# specgen printed, so captured == golden proves the captured file is real tool
# output too, with no second execution and no trust required.
#
# Driven through CMake rather than a shell script so the checks work on every
# platform the project builds on, matching run-golden.cmake.

# Compare two directories: the set of files, then each file's bytes.
function(compare_trees actual expected what)
    if(NOT EXISTS "${expected}")
        message(FATAL_ERROR "missing captured directory ${expected}")
    endif()
    if(NOT EXISTS "${actual}")
        message(FATAL_ERROR "${what} produced no directory at ${actual}")
    endif()

    file(GLOB_RECURSE actual_files RELATIVE "${actual}" "${actual}/*")
    file(GLOB_RECURSE expected_files RELATIVE "${expected}" "${expected}/*")
    list(SORT actual_files)
    list(SORT expected_files)

    if(NOT actual_files STREQUAL expected_files)
        message(
            FATAL_ERROR
            "${what}: file set mismatch\n"
            "  expected: ${expected_files}\n"
            "  actual:   ${actual_files}\n"
            "If the new set is correct, re-run examples/cli/run-all.sh."
        )
    endif()

    foreach(f IN LISTS expected_files)
        execute_process(
            COMMAND
                "${CMAKE_COMMAND}" -E compare_files "${actual}/${f}"
                "${expected}/${f}"
            RESULT_VARIABLE compare_result
        )
        if(NOT compare_result EQUAL 0)
            message(
                FATAL_ERROR
                "${what}: mismatch in ${f}\n"
                "  expected: ${expected}/${f}\n"
                "  actual:   ${actual}/${f}\n"
                "  diff -u '${expected}/${f}' '${actual}/${f}'\n"
                "If the new output is correct, re-run examples/cli/run-all.sh."
            )
        endif()
    endforeach()
endfunction()

if(MODE STREQUAL "tree")
    if(NOT ACTUAL OR NOT EXPECTED)
        message(
            FATAL_ERROR
            "run-example.cmake: tree mode needs ACTUAL and EXPECTED"
        )
    endif()
    compare_trees("${ACTUAL}" "${EXPECTED}" "captured fragments")
elseif(MODE STREQUAL "rerun")
    if(NOT SPECGEN OR NOT SCRIPT OR NOT SECTION OR NOT CAPTURED OR NOT SCRATCH)
        message(
            FATAL_ERROR
            "run-example.cmake: rerun mode needs SPECGEN, SCRIPT, SECTION, CAPTURED and SCRATCH"
        )
    endif()

    # A scratch tree of this run's own. The script rewrites its own section
    # directory, but the parent is emptied here so a renamed output file cannot
    # survive from a previous run and be compared against itself.
    file(REMOVE_RECURSE "${SCRATCH}")
    file(MAKE_DIRECTORY "${SCRATCH}")

    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E env "SPECGEN=${SPECGEN}"
            "EXAMPLES_OUT=${SCRATCH}" "${SCRIPT}"
        RESULT_VARIABLE script_result
        ERROR_VARIABLE script_error
    )
    if(NOT script_result EQUAL 0)
        message(
            FATAL_ERROR
            "${SCRIPT} failed (exit ${script_result}):\n${script_error}"
        )
    endif()

    compare_trees("${SCRATCH}/${SECTION}" "${CAPTURED}" "${SECTION}")
else()
    message(FATAL_ERROR "run-example.cmake: unknown MODE '${MODE}'")
endif()
