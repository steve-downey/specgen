# tests/golden/run-apply-to-draft.cmake                           -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# --apply-to-draft (issue #343): the paper whose whole change to
# [func.wrap.move] is `constexpr` (tests/corpus/diff/func_wrap_move_asdraft.hpp)
# applied to the draft's source, the corpus slice staged as `utilities.tex`.
# The edited file is pinned byte for byte, and must differ from the slice in
# nothing but what the paper changes: every line it changes is a draft line
# with `constexpr ` added, but for the call operator's, and every other line,
# index entries and comments included, is the draft's own. The header's helper types are declared
# outside every subclause, so they are left out with a warning. Then the
# usage errors.
#
# With -DUPDATE_GOLDEN=ON the edited file is written over EXPECTED.
file(REMOVE_RECURSE "${SCRATCH}")
file(MAKE_DIRECTORY "${SCRATCH}/draft")
file(COPY_FILE "${SLICE}" "${SCRATCH}/draft/utilities.tex")

function(run expected_result stderr_pattern)
    execute_process(
        COMMAND "${SPECGEN}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        WORKING_DIRECTORY "${SCRATCH}"
    )
    if(
        NOT result EQUAL expected_result
        OR NOT error MATCHES "${stderr_pattern}"
    )
        message(FATAL_ERROR "${ARGN}: exit ${result}, stderr: ${error}")
    endif()
endfunction()

run(0 "utilities.tex: [0-9]+ edits" generate "${HEADER}" --no-compile-commands
    --apply-to-draft "${SCRATCH}/draft" -o "${SCRATCH}/edited"
)
set(edited "${SCRATCH}/edited/utilities.tex")
if(UPDATE_GOLDEN)
    file(COPY_FILE "${edited}" "${EXPECTED}")
    return()
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${edited}" "${EXPECTED}"
    RESULT_VARIABLE differs
)
if(differs)
    message(FATAL_ERROR "${edited} differs from ${EXPECTED}")
endif()

# Line by line against the slice: the same number of lines, and each that
# changed is the draft's with `constexpr ` added. Brackets and semicolons are
# list syntax to CMake, so the lines are compared with them replaced.
foreach(name slice edited)
    if(name STREQUAL slice)
        file(STRINGS "${SLICE}" raw)
    else()
        file(STRINGS "${edited}" raw)
    endif()
    string(REPLACE "[" "<" raw "${raw}")
    string(REPLACE "]" ">" raw "${raw}")
    set(${name}_lines "${raw}")
endforeach()
list(LENGTH slice_lines n)
list(LENGTH edited_lines m)
if(NOT n EQUAL m)
    message(FATAL_ERROR "the edit changed the number of lines: ${n} -> ${m}")
endif()
set(changed 0)
math(EXPR last "${n} - 1")
foreach(i RANGE ${last})
    list(GET slice_lines ${i} before)
    list(GET edited_lines ${i} after)
    if(NOT before STREQUAL after)
        string(REPLACE "constexpr " "" stripped "${after}")
        # The one other change the paper makes: its call operator is a single
        # specialization's, with none of the draft's placeholders, which the
        # wording diff shows removed as well.
        if(after STREQUAL "constexpr R operator()(ArgTypes... args);")
            math(EXPR changed "${changed} + 1")
            continue()
        endif()
        if(NOT stripped STREQUAL before)
            message(
                FATAL_ERROR
                "line ${i} changed in more than `constexpr`:\n  ${before}\n  ${after}"
            )
        endif()
        math(EXPR changed "${changed} + 1")
    endif()
endforeach()
if(changed EQUAL 0)
    message(FATAL_ERROR "no line changed")
endif()

# The usage errors: the edited draft is the whole output.
run(2 "needs -o" generate "${HEADER}" --no-compile-commands --apply-to-draft "${SCRATCH}/draft")
run(2 "takes no baseline" generate "${HEADER}" --no-compile-commands --apply-to-draft "${SCRATCH}/draft"
    -o "${SCRATCH}/x" --baseline-draft "${SCRATCH}/draft"
)
run(2 "takes no baseline" generate "${HEADER}" --no-compile-commands --apply-to-draft "${SCRATCH}/draft"
    -o "${SCRATCH}/x" --inherit-draft "${SCRATCH}/draft"
)
run(2 "--split, --validate and --paper do not apply" generate "${HEADER}" --no-compile-commands
    --apply-to-draft "${SCRATCH}/draft" -o "${SCRATCH}/x" --validate
)
run(2 "--emit-ir writes IR" generate "${HEADER}" --no-compile-commands --emit-ir
    --apply-to-draft "${SCRATCH}/draft"
)
