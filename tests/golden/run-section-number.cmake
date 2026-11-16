# tests/golden/run-section-number.cmake                             -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# --section-number at the driver seam (issues #265, #287). The byte-exact
# golden `section_number` pins the framing of one numbered section; this pins
# what a single render cannot: that `generate` and `render` agree, that the
# number reaches every `--split` fragment and leaves the whole unchanged, that
# a wording diff carries it through both baseline routes, that a number for a
# section the diff leaves out is not a mistake, and that every malformed or
# repeated assignment is a usage error from both commands.
#
# Ported from the second implementation of #265, which merged into its base
# branch and never reached main, and adapted to the rules the first one
# landed: a name matching no top-level section and a non-latex backend are
# errors here (the `golden.section_number.refuses.*` cases), not warnings.
file(MAKE_DIRECTORY "${SCRATCH}")
set(header "${SOURCE_DIR}/tests/corpus/diff/gadget.after.hpp")
set(ir "${SOURCE_DIR}/tests/golden/diff_gadget_after/expected.json")
set(number_args
    --section-number
    gadget.observe=22.10.17.4
    --section-number
    gadget.mod=22.10.17.5
)

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
    set(output "${output}" PARENT_SCOPE)
endfunction()

# One pass and two passes write the same numbered wording, with a hook ahead
# of each named section.
run(0 "^$" generate "${header}" --no-compile-commands ${number_args})
set(generated "${output}")
run(0 "^$" render --from-ir "${ir}" ${number_args})
if(NOT output STREQUAL generated)
    message(FATAL_ERROR "generate and render disagree on section numbers")
endif()
foreach(pair "22.10.17.4;gadget.observe" "22.10.17.5;gadget.mod")
    list(GET pair 0 number)
    list(GET pair 1 stable)
    set(hook
        "\\ifdefined\\clausenumber\\clausenumber{${number}}\\fi\n\\rSec3[${stable}]"
    )
    string(FIND "${output}" "${hook}" found)
    if(found LESS 0)
        message(FATAL_ERROR "missing hook for ${stable}")
    endif()
endforeach()

# They agree on each fragment too, and asking for the pieces does not change
# the whole.
run(0 "^$" generate "${header}" --no-compile-commands ${number_args}
    --split "${SCRATCH}/generated" -o "${SCRATCH}/generated.tex"
)
run(0 "^$" render --from-ir "${ir}" ${number_args}
    --split "${SCRATCH}/rendered" -o "${SCRATCH}/rendered.tex"
)
foreach(stable gadget.observe gadget.mod gadget.query)
    file(READ "${SCRATCH}/generated/${stable}.tex" generated_fragment)
    file(READ "${SCRATCH}/rendered/${stable}.tex" rendered_fragment)
    if(NOT generated_fragment STREQUAL rendered_fragment)
        message(FATAL_ERROR "generate and render fragments disagree: ${stable}")
    endif()
endforeach()
foreach(stable gadget.observe gadget.mod)
    file(READ "${SCRATCH}/rendered/${stable}.tex" fragment)
    if(NOT fragment MATCHES "^.ifdefined.clausenumber")
        message(
            FATAL_ERROR
            "the fragment ${stable} does not open with its hook"
        )
    endif()
endforeach()
file(READ "${SCRATCH}/rendered/gadget.query.tex" fragment)
if(fragment MATCHES "clausenumber")
    message(FATAL_ERROR "an unnumbered fragment received a hook")
endif()
file(READ "${SCRATCH}/generated.tex" generated_whole)
file(READ "${SCRATCH}/rendered.tex" rendered_whole)
if(
    NOT generated_whole STREQUAL generated
    OR NOT rendered_whole STREQUAL generated
)
    message(FATAL_ERROR "--split changed whole-document output")
endif()

# A numbered wording diff carries the hook through both baseline routes.
run(0 "^$" generate "${header}" --no-compile-commands ${number_args}
    --baseline-header "${SOURCE_DIR}/tests/corpus/diff/gadget.before.hpp"
    --diff-context decls
)
set(generated_diff "${output}")
run(0 "^$" render --from-ir "${ir}" ${number_args}
    --baseline-ir "${SOURCE_DIR}/tests/golden/diff_gadget_before/expected.json"
    --diff-context decls
)
if(NOT output STREQUAL generated_diff OR NOT output MATCHES "clausenumber")
    message(
        FATAL_ERROR
        "generate and render disagree on numbered wording diffs"
    )
endif()

# A number for a section the diff leaves out is not a mistake: the name is a
# section of the paper, checked before the context drops what did not change.
# A self-diff drops everything, and still succeeds, with nothing to print.
run(0 "^$" render --from-ir "${ir}" --baseline-ir "${ir}" ${number_args}
    --diff-context decls
)
if(NOT output STREQUAL "")
    message(FATAL_ERROR "a self-diff should be empty")
endif()

# A malformed, repeated or missing assignment is a usage error from both
# commands, and so is the option beside --emit-ir.
foreach(command generate render)
    if(command STREQUAL generate)
        set(input_args "${header}" --no-compile-commands)
    else()
        set(input_args --from-ir "${ir}")
    endif()
    foreach(invalid "gadget.observe" "=22.1")
        run(2 "--section-number takes" ${command} ${input_args}
            --section-number "${invalid}"
        )
    endforeach()
    foreach(
        invalid
        "gadget.observe="
        "gadget.observe=22."
        "gadget.observe=22..1"
        "gadget.observe=-1"
        "gadget.observe=22.x"
        "gadget.observe=22{1}"
    )
        run(2 "is not a dotted number" ${command} ${input_args}
            --section-number "${invalid}"
        )
    endforeach()
    run(2 "a number twice" ${command} ${input_args} ${number_args}
        --section-number gadget.observe=22.1
    )
    run(2 "--section-number" ${command} ${input_args} --section-number)
endforeach()
run(2 "--section-number does not apply" generate "${header}"
    --no-compile-commands --emit-ir --section-number gadget.observe=22.1
)

# A paper of several documents recognizes every document's top-level names.
file(
    WRITE "${SCRATCH}/other.json"
    "{\"nodes\":[{\"type\":\"section\",\"stable\":\"other\",\"title\":\"Other\",\"children\":[]}]}"
)
run(0 "^$" render --from-ir "${ir}" --from-ir "${SCRATCH}/other.json"
    ${number_args} --section-number other=22.10.17.6
)
if(NOT output MATCHES "clausenumber[{]22.10.17.6[}]")
    message(FATAL_ERROR "a sibling document's number was lost")
endif()
