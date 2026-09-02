# tests/golden/run-single-pass.cmake                               -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The single-pass gate for one generate-mode golden case: rendering a header in
# one process must produce exactly the bytes that rendering its checked-in IR
# does.
#
#   two-pass:  specgen render --from-ir <case>/expected.json
#   one-pass:  specgen generate <header>
#
# The IR is the boundary between the front end and the backends (decision
# ir-boundary), and `generate` without `--emit-ir` crosses it in memory instead
# of through a file. Nothing but the carrier differs, so the two outputs must
# agree byte for byte -- and this case says so with no third checked-in file to
# maintain: the two-pass side is regenerated from the golden IR on every run,
# which is itself pinned by golden.<case>.
#
# HEADER, IR and ACTUAL are required; EXTRA_ARGS (space-separated, appended
# after a `--`) and COMPILE_COMMANDS_DIR are the same two opt-ins
# specgen_add_golden documents for the generate mode, passed through so a case
# that needs include paths is gated like every other one.
#
# `--no-compile-commands` is unconditional here for the reason it is in
# run-golden.cmake: the repository keeps a gitignored compile_commands.json
# symlink at its root, and a probe for it would make this comparison depend on
# whether a developer happens to have a build configured.

if(NOT SPECGEN OR NOT HEADER OR NOT IR OR NOT ACTUAL)
    message(
        FATAL_ERROR
        "run-single-pass.cmake: SPECGEN, HEADER, IR, and ACTUAL are required"
    )
endif()

execute_process(
    COMMAND "${SPECGEN}" render --from-ir "${IR}" -o "${ACTUAL}.two-pass"
    RESULT_VARIABLE two_pass_result
    ERROR_VARIABLE two_pass_error
)
if(NOT two_pass_result EQUAL 0)
    message(
        FATAL_ERROR
        "rendering the golden IR failed (exit ${two_pass_result}):\n${two_pass_error}"
    )
endif()

set(generate_args
    generate
    "${HEADER}"
    -o
    "${ACTUAL}.one-pass"
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
    RESULT_VARIABLE one_pass_result
    ERROR_VARIABLE one_pass_error
)
if(NOT one_pass_result EQUAL 0)
    message(
        FATAL_ERROR
        "generating wording in one pass failed (exit ${one_pass_result}):\n${one_pass_error}"
    )
endif()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E compare_files "${ACTUAL}.two-pass"
        "${ACTUAL}.one-pass"
    RESULT_VARIABLE compare_result
)
if(NOT compare_result EQUAL 0)
    find_program(DIFF_TOOL diff)
    if(DIFF_TOOL)
        execute_process(
            COMMAND "${DIFF_TOOL}" -u "${ACTUAL}.two-pass" "${ACTUAL}.one-pass"
            OUTPUT_VARIABLE diff_text
            ERROR_QUIET
        )
    endif()
    message(
        FATAL_ERROR
        "single-pass wording differs from the two-pass route\n"
        "  two-pass: ${ACTUAL}.two-pass (render --from-ir ${IR})\n"
        "  one-pass: ${ACTUAL}.one-pass (generate ${HEADER})\n"
        "  diff -u '${ACTUAL}.two-pass' '${ACTUAL}.one-pass'\n"
        "${diff_text}"
    )
endif()
