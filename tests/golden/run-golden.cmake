# tests/golden/run-golden.cmake                                    -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Run one golden case, in one of three MODEs:
#   render    (default) INPUT is IR JSON; run it through `specgen render`.
#   generate  INPUT is a header; run it through `specgen generate --emit-ir`
#             (Clang-tier only — the case registering this mode must itself be
#             registered only where the driver is built). Always passes
#             `--no-compile-commands`: the repository keeps a
#             gitignored `compile_commands.json` symlink at its root, and
#             `specgen generate`'s own default is to probe for one, so a
#             golden that omitted this would parse differently depending on
#             whether a developer happens to have configured a build — exactly
#             the nondeterminism a golden suite must not have. EXTRA_ARGS (a
#             space-separated string, split with separate_arguments) is
#             appended after a `--`, and COMPILE_COMMANDS_DIR, if set, is
#             passed as `--compile-commands <dir>`; both come from
#             specgen_add_golden()'s matching arguments and are how a
#             generate-mode case exercises the two ways compiler
#             arguments reach the front end.
#   diagnose  INPUT is a header; run the same `generate --emit-ir` but compare
#             its *stderr* — the build-time findings — instead of the
#             IR, which goes to a scratch file. Clang-tier only, like generate.
#             Run from the header's own directory so the paths specgen prints
#             are the file name alone: an absolute build path would make the
#             golden machine-specific. Also always passes
#             `--no-compile-commands`, for the same reason `generate` does —
#             this mode's whole point is a byte-exact stderr comparison, which
#             a stray "using compile flags..." diagnostic would break on any
#             machine that happens to have a build configured.
#   split     INPUT is IR JSON; run it through `specgen render --split`
#             and compare the *directory* of fragments it writes, plus the
#             manifest it prints, against a checked-in directory. ACTUAL is a
#             scratch directory rather than a file, and specgen is run from
#             inside it with a relative `--split wording`, so the manifest
#             holds `wording/optional.ctor.tex` and not a build path. Design
#             §8 says regeneration is *wholesale and idempotent*, so the run
#             happens twice into the same directory and the two manifests must
#             agree — a second write that changed anything would show up as a
#             mismatch against the goldens, which were captured from a first
#             run into an empty one. Needs no Clang, like render.
#   validate  INPUT is IR JSON; run it through
#             `specgen render --from-ir INPUT --validate`, capturing stderr
#             (stdout discarded) and requiring exit code 1 — needs no Clang,
#             like `render`. Pass EXPECT_EXIT=0 for a fixture whose findings
#             are all below Error severity: `--validate` reports those and
#             still renders, so exit 0 *is* that fixture's expected outcome,
#             and pinning it is what keeps a warning-severity rule
#             from silently becoming an output-suppressing one.
# Either way the result is compared against EXPECTED, byte for byte. With
# -DUPDATE_GOLDEN=ON the actual output replaces EXPECTED instead, which is how
# `make goldens` regenerates them.
#
# Driven through CMake rather than a shell script so the harness works
# unchanged on every platform the project builds on.

if(NOT SPECGEN OR NOT INPUT OR NOT EXPECTED OR NOT ACTUAL)
    message(
        FATAL_ERROR
        "run-golden.cmake: SPECGEN, INPUT, EXPECTED, and ACTUAL are required"
    )
endif()

if(NOT MODE)
    set(MODE render)
endif()

if(MODE STREQUAL "generate")
    # --no-compile-commands is unconditional (see the mode's note
    # above); COMPILE_COMMANDS_DIR and EXTRA_ARGS are each this case's own
    # opt-in to the other two sources, and specgen_add_golden's own precedence
    # documentation applies (a caller setting EXTRA_ARGS is not asking
    # COMPILE_COMMANDS_DIR to be second-guessed either, so both may be passed
    # at once only when a case genuinely wants to exercise that).
    set(generate_args
        generate
        --emit-ir
        "${INPUT}"
        -o
        "${ACTUAL}"
        --no-compile-commands
    )
    if(COMPILE_COMMANDS_DIR)
        list(APPEND generate_args --compile-commands "${COMPILE_COMMANDS_DIR}")
    endif()
    if(EXTRA_ARGS)
        separate_arguments(extra_args_list UNIX_COMMAND "${EXTRA_ARGS}")
        list(APPEND generate_args -- ${extra_args_list})
    endif()
    execute_process(
        COMMAND "${SPECGEN}" ${generate_args}
        RESULT_VARIABLE render_result
        ERROR_VARIABLE render_error
    )
elseif(MODE STREQUAL "diagnose")
    # The findings are the artifact here, so the IR is the thing discarded.
    # cmake_path keeps the invocation relative to the header's directory; see
    # the mode's note at the top of this file for why that matters.
    cmake_path(GET INPUT PARENT_PATH input_dir)
    cmake_path(GET INPUT FILENAME input_name)
    execute_process(
        COMMAND
            "${SPECGEN}" generate --emit-ir "${input_name}" -o "${ACTUAL}.json"
            --no-compile-commands
        WORKING_DIRECTORY "${input_dir}"
        RESULT_VARIABLE render_result
        ERROR_FILE "${ACTUAL}"
    )
elseif(MODE STREQUAL "render")
    if(NOT BACKEND)
        set(BACKEND latex)
    endif()

    # Paper mode is an extra render flag, not a mode of its own -- the
    # comparison, the exit code and the expected file all behave exactly as
    # they do without it.
    set(paper_args "")
    if(PAPER)
        set(paper_args "--paper")
    endif()

    execute_process(
        COMMAND
            "${SPECGEN}" render --from-ir "${INPUT}" --backend "${BACKEND}"
            ${paper_args} -o "${ACTUAL}"
        RESULT_VARIABLE render_result
        ERROR_VARIABLE render_error
    )
elseif(MODE STREQUAL "split")
    if(NOT BACKEND)
        set(BACKEND latex)
    endif()

    set(root_args "")
    if(ROOT)
        set(root_args --root "${ROOT}")
    endif()

    # A scratch directory of this run's own, emptied first: idempotence is
    # what this mode tests, and inheriting a previous *version's* output would
    # test something else.
    file(REMOVE_RECURSE "${ACTUAL}")
    file(MAKE_DIRECTORY "${ACTUAL}")

    execute_process(
        COMMAND
            "${SPECGEN}" render --from-ir "${INPUT}" --backend "${BACKEND}"
            ${root_args} --split wording
        WORKING_DIRECTORY "${ACTUAL}"
        RESULT_VARIABLE render_result
        OUTPUT_FILE "${ACTUAL}/manifest.first"
        ERROR_VARIABLE render_error
    )
    if(render_result EQUAL 0)
        execute_process(
            COMMAND
                "${SPECGEN}" render --from-ir "${INPUT}" --backend "${BACKEND}"
                ${root_args} --split wording
            WORKING_DIRECTORY "${ACTUAL}"
            RESULT_VARIABLE render_result
            OUTPUT_FILE "${ACTUAL}/manifest"
            ERROR_VARIABLE render_error
        )
    endif()
elseif(MODE STREQUAL "validate")
    # --validate reports to stderr and exits 1 when a finding is at error
    # severity, which is the whole point of this mode — that is the "success"
    # this case checks for, not a failure. Stdout is quiet by construction
    # (rendering is skipped once any Error finding is reported).
    execute_process(
        COMMAND "${SPECGEN}" render --from-ir "${INPUT}" --validate
        RESULT_VARIABLE render_result
        OUTPUT_QUIET
        ERROR_FILE "${ACTUAL}"
    )
else()
    message(FATAL_ERROR "run-golden.cmake: unknown MODE '${MODE}'")
endif()

if(DEFINED EXPECT_EXIT)
    set(expected_result "${EXPECT_EXIT}")
elseif(MODE STREQUAL "validate")
    set(expected_result 1)
else()
    set(expected_result 0)
endif()

if(NOT render_result EQUAL expected_result)
    if(NOT DEFINED render_error AND EXISTS "${ACTUAL}")
        file(READ "${ACTUAL}" render_error)
    endif()
    message(
        FATAL_ERROR
        "specgen failed (expected exit ${expected_result}, got ${render_result}):\n${render_error}"
    )
endif()

# The split mode compares a directory and a manifest rather than one file, so
# it does its own updating and comparing and returns before the single-file
# tail below.
if(MODE STREQUAL "split")
    if(UPDATE_GOLDEN)
        file(REMOVE_RECURSE "${EXPECTED}")
        file(COPY "${ACTUAL}/wording/" DESTINATION "${EXPECTED}")
        configure_file("${ACTUAL}/manifest" "${EXPECTED}.manifest" COPYONLY)
        message(STATUS "updated ${EXPECTED}")
        return()
    endif()

    if(NOT EXISTS "${EXPECTED}")
        message(
            FATAL_ERROR
            "missing golden directory ${EXPECTED}; run `make goldens` to create it"
        )
    endif()

    # Wholesale and idempotent (design §8): the second run wrote the same
    # manifest as the first, and — since the goldens below were captured from a
    # first run into an empty directory — the same bytes into the same files.
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E compare_files "${ACTUAL}/manifest.first"
            "${ACTUAL}/manifest"
        RESULT_VARIABLE compare_result
    )
    if(NOT compare_result EQUAL 0)
        message(
            FATAL_ERROR
            "regenerating twice was not a no-op: the two runs printed different manifests\n"
            "  diff -u '${ACTUAL}/manifest.first' '${ACTUAL}/manifest'"
        )
    endif()

    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E compare_files "${ACTUAL}/manifest"
            "${EXPECTED}.manifest"
        RESULT_VARIABLE compare_result
    )
    if(NOT compare_result EQUAL 0)
        message(
            FATAL_ERROR
            "fragment manifest mismatch\n"
            "  expected: ${EXPECTED}.manifest\n"
            "  actual:   ${ACTUAL}/manifest\n"
            "  diff -u '${EXPECTED}.manifest' '${ACTUAL}/manifest'\n"
            "If the new output is correct, run `make goldens` to update."
        )
    endif()

    # The manifest agrees; now the set of files on disk and their contents.
    # The file *set* is checked separately from the manifest because a stale
    # fragment left behind by an earlier run is exactly what a manifest cannot
    # report.
    file(GLOB actual_files RELATIVE "${ACTUAL}/wording" "${ACTUAL}/wording/*")
    file(GLOB expected_files RELATIVE "${EXPECTED}" "${EXPECTED}/*")
    list(SORT actual_files)
    list(SORT expected_files)
    if(NOT actual_files STREQUAL expected_files)
        message(
            FATAL_ERROR
            "fragment set mismatch\n"
            "  expected: ${expected_files}\n"
            "  actual:   ${actual_files}\n"
            "If the new set is correct, run `make goldens` to update."
        )
    endif()

    foreach(fragment IN LISTS expected_files)
        execute_process(
            COMMAND
                "${CMAKE_COMMAND}" -E compare_files
                "${ACTUAL}/wording/${fragment}" "${EXPECTED}/${fragment}"
            RESULT_VARIABLE compare_result
        )
        if(NOT compare_result EQUAL 0)
            message(
                FATAL_ERROR
                "golden mismatch in fragment ${fragment}\n"
                "  expected: ${EXPECTED}/${fragment}\n"
                "  actual:   ${ACTUAL}/wording/${fragment}\n"
                "  diff -u '${EXPECTED}/${fragment}' '${ACTUAL}/wording/${fragment}'\n"
                "If the new output is correct, run `make goldens` to update."
            )
        endif()
    endforeach()
    return()
endif()

if(UPDATE_GOLDEN)
    configure_file("${ACTUAL}" "${EXPECTED}" COPYONLY)
    message(STATUS "updated ${EXPECTED}")
    return()
endif()

if(NOT EXISTS "${EXPECTED}")
    message(
        FATAL_ERROR
        "missing golden ${EXPECTED}; run `make goldens` to create it"
    )
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${ACTUAL}" "${EXPECTED}"
    RESULT_VARIABLE compare_result
)

if(NOT compare_result EQUAL 0)
    # message() reflows its argument, which mangles LaTeX beyond reading, so
    # report locations and let the reader use a real diff tool. Try to show one
    # if `diff` is present; either way the paths are the useful part.
    find_program(DIFF_TOOL diff)
    if(DIFF_TOOL)
        execute_process(
            COMMAND "${DIFF_TOOL}" -u "${EXPECTED}" "${ACTUAL}"
            OUTPUT_VARIABLE diff_text
            ERROR_QUIET
        )
    endif()
    message(
        FATAL_ERROR
        "golden mismatch\n"
        "  expected: ${EXPECTED}\n"
        "  actual:   ${ACTUAL}\n"
        "  diff -u '${EXPECTED}' '${ACTUAL}'\n"
        "${diff_text}\n"
        "If the new output is correct, run `make goldens` to update."
    )
endif()
