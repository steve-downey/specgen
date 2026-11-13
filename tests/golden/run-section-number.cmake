# tests/golden/run-section-number.cmake                             -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Exercise the driver seam, including the paper-wide name lookup before a
# diff removes unchanged sections. The byte-exact golden cases pin framing.
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

# Generate and render must also agree on each fragment and the whole output.
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
file(READ "${SCRATCH}/generated.tex" generated_whole)
file(READ "${SCRATCH}/rendered.tex" rendered_whole)
if(
    NOT generated_whole STREQUAL generated
    OR NOT rendered_whole STREQUAL generated
)
    message(FATAL_ERROR "--split changed whole-document output")
endif()

# Numbered wording diffs use the same hook through both baseline paths.
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

# A valid name is checked before the diff context drops its unchanged section.
run(0 "^$" render --from-ir "${ir}" --baseline-ir "${ir}" ${number_args} --diff-context decls)
if(NOT output STREQUAL "")
    message(FATAL_ERROR "a self-diff should be empty")
endif()

# Invalid syntax and duplicate assignments are usage errors in both commands.
foreach(command generate render)
    if(command STREQUAL generate)
        set(input_args "${header}" --no-compile-commands)
    else()
        set(input_args --from-ir "${ir}")
    endif()
    foreach(
        invalid
        "gadget.observe"
        "=22.1"
        "gadget.observe="
        "gadget.observe=22."
        "gadget.observe=22..1"
        "gadget.observe=-1"
        "gadget.observe=22.x"
        "gadget.observe=22{1}"
    )
        run(2 "--section-number takes" ${command} ${input_args} --section-number "${invalid}")
    endforeach()
    run(2 "number twice" ${command} ${input_args} ${number_args} --section-number gadget.observe=22.1)
    run(2 "--section-number" ${command} ${input_args} --section-number)
    run(0 "warning: --section-number names 'missing'" ${command} ${input_args} --section-number missing=22.1)
    foreach(backend mpark org)
        run(0 "^$" ${command} ${input_args} --backend ${backend})
        set(plain "${output}")
        run(0 "warning: --section-number numbers latex headings only" ${command} ${input_args}
            --backend ${backend} ${number_args}
        )
        if(NOT output STREQUAL plain)
            message(FATAL_ERROR "--section-number changed ${backend} output")
        endif()
    endforeach()
endforeach()
run(2 "--section-number does not apply" generate "${header}" --no-compile-commands
    --emit-ir --section-number gadget.observe=22.1
)

# Nested names do not get hooks; a multi-document invocation recognizes all
# of its top-level names, without warning once per unrelated document.
set(nested "${SOURCE_DIR}/tests/golden/base_level/input.json")
run(0 "warning: --section-number names 'gadget.observe.ready'" render --from-ir "${nested}"
    --section-number gadget.observe.ready=22.10.17.4.1
)
if(output MATCHES "clausenumber")
    message(FATAL_ERROR "a nested section received a hook")
endif()
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
