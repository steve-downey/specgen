# tests/golden/run-whole-is-fragments.cmake                        -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The two views of one render are the same wording: the whole a paper writes
# with `-o` must be its `--split` fragments, in manifest order, joined by a
# blank line.
#
#   whole:      specgen render --from-ir A --from-ir B -o whole.tex
#   fragments:  specgen render --from-ir A --from-ir B --split wording/
#
# Pinned as its own case rather than left to the two goldens that already hold
# these bytes, because what it is about is the relationship between them, and
# nothing in either file's own expected text says what the other should be. A
# change to the join -- the separator, the order, a stray document-level frame
# around one view and not the other -- would regenerate both goldens happily
# and leave a paper that assembles its clause from fragments disagreeing with
# one that renders it whole. Downstream projects rely on exactly this: the
# assembled clause is the thing they diff against the working draft, and the
# fragments are what their paper `\input`s.
#
# SPECGEN, INPUTS (|-joined IR paths) and ACTUAL (a scratch directory) are
# required; ROOTS (|-joined, one per input in order) is needed whenever the
# documents would otherwise derive the same root fragment name, which a paper's
# headers routinely do -- they share a stable-name prefix.

if(NOT SPECGEN OR NOT INPUTS OR NOT ACTUAL)
    message(
        FATAL_ERROR
        "run-whole-is-fragments.cmake: SPECGEN, INPUTS, and ACTUAL are required"
    )
endif()

string(REPLACE "|" ";" _inputs "${INPUTS}")
set(input_args "")
foreach(_input IN LISTS _inputs)
    list(APPEND input_args --from-ir "${_input}")
endforeach()
if(ROOTS)
    string(REPLACE "|" ";" _roots "${ROOTS}")
    foreach(_root IN LISTS _roots)
        list(APPEND input_args --root "${_root}")
    endforeach()
endif()

file(REMOVE_RECURSE "${ACTUAL}")
file(MAKE_DIRECTORY "${ACTUAL}")

# One invocation writing both, which is the invocation the claim is about: the
# whole and the pieces come out of one parse and one split, so a difference
# between them cannot be blamed on two runs having seen different input.
execute_process(
    COMMAND
        "${SPECGEN}" render ${input_args} -o whole.tex --split wording
    WORKING_DIRECTORY "${ACTUAL}"
    OUTPUT_VARIABLE manifest
    RESULT_VARIABLE render_result
    ERROR_VARIABLE render_error
)
if(NOT render_result EQUAL 0)
    message(
        FATAL_ERROR
        "rendering the paper failed (exit ${render_result}):\n${render_error}"
    )
endif()

# The manifest is the fragment order; reading it rather than globbing the
# directory is the point, since the order is what the join has to match.
string(REPLACE "\n" ";" manifest_lines "${manifest}")
set(joined "")
set(separator "")
foreach(line IN LISTS manifest_lines)
    if(line STREQUAL "")
        continue()
    endif()
    file(READ "${ACTUAL}/${line}" fragment_text)
    string(APPEND joined "${separator}${fragment_text}")
    set(separator "\n")
endforeach()

file(READ "${ACTUAL}/whole.tex" whole_text)

if(NOT joined STREQUAL whole_text)
    file(WRITE "${ACTUAL}/joined.tex" "${joined}")
    find_program(DIFF_TOOL diff)
    if(DIFF_TOOL)
        execute_process(
            COMMAND "${DIFF_TOOL}" -u "${ACTUAL}/joined.tex" "${ACTUAL}/whole.tex"
            OUTPUT_VARIABLE diff_text
            ERROR_QUIET
        )
    endif()
    message(
        FATAL_ERROR
        "the whole is not its fragments joined\n"
        "  fragments: ${ACTUAL}/joined.tex (manifest order, blank-line separated)\n"
        "  whole:     ${ACTUAL}/whole.tex\n"
        "  diff -u '${ACTUAL}/joined.tex' '${ACTUAL}/whole.tex'\n"
        "${diff_text}"
    )
endif()
