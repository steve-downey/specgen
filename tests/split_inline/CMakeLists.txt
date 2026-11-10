# tests/split_inline/CMakeLists.txt                                -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# split_inline's cases (docs/plans/split-inline.md, "Checking a conversion"):
# a header in, the converted header out, compared byte for byte; the
# conversion a fixed point of the tool; and, for a header in specgen form,
# the wording specgen renders from it unchanged by the conversion. Registered
# only where the tool is built, like the driver's goldens.

# The pinned LLVM's own clang, for the `.compiles` siblings: the converted
# header must still be C++. Absent, those siblings are not registered.
find_program(
    SPLIT_INLINE_CLANG
    NAMES clang
    HINTS "${LLVM_TOOLS_BINARY_DIR}"
    NO_DEFAULT_PATH
)
if(NOT SPLIT_INLINE_CLANG)
    message(
        STATUS
        "beman.specgen: no clang in ${LLVM_TOOLS_BINARY_DIR}; skipping the split_inline .compiles siblings"
    )
endif()

# split_inline_add_case(<case> [MOVE_COMMENTS] [SAME_WORDING] [REFUSALS]
#                       [INPUT <path>])
#
# Registers `split_inline.<case>`: run the tool over
# `cases/<case>/input.hpp` (or INPUT, a corpus header) and compare with
# `cases/<case>/expected.hpp`. With it, `split_inline.<case>.fixed_point`
# and, with clang, `split_inline.<case>.compiles`. MOVE_COMMENTS passes
# `--move-comments`. SAME_WORDING adds `split_inline.<case>.same_wording`,
# which renders both headers through the driver. REFUSALS is a case whose
# point is what the tool will not move: exit status 1 is required and
# standard error is compared with `cases/<case>/expected.stderr`.
function(split_inline_add_case case)
    set(options MOVE_COMMENTS SAME_WORDING REFUSALS)
    set(one_value_args INPUT)
    cmake_parse_arguments(ARG "${options}" "${one_value_args}" "" ${ARGN})
    set(case_dir "${CMAKE_CURRENT_SOURCE_DIR}/cases/${case}")
    set(input "${case_dir}/input.hpp")
    if(ARG_INPUT)
        set(input "${ARG_INPUT}")
    endif()
    set(expected "${case_dir}/expected.hpp")
    set(scratch "${CMAKE_CURRENT_BINARY_DIR}/${case}")
    set(tool_options "")
    if(ARG_MOVE_COMMENTS)
        list(APPEND tool_options --move-comments)
    endif()
    set(common
        "-DTOOL=$<TARGET_FILE:split_inline>"
        "-DINPUT=${input}"
        "-DEXPECTED=${expected}"
        "-DOPTIONS=${tool_options}"
    )
    set(convert_extra "")
    if(ARG_REFUSALS)
        # Refused members are refused again on the second run, so both runs
        # exit 1; the convert run also pins the diagnostics.
        list(APPEND common "-DEXPECTED_STATUS=1")
        list(
            APPEND convert_extra
            "-DEXPECTED_STDERR=${case_dir}/expected.stderr"
        )
    endif()
    add_test(
        NAME split_inline.${case}
        COMMAND
            ${CMAKE_COMMAND} -DMODE=convert ${common} ${convert_extra}
            "-DACTUAL=${scratch}/actual.hpp" -P
            "${CMAKE_CURRENT_SOURCE_DIR}/run-case.cmake"
    )
    add_test(
        NAME split_inline.${case}.fixed_point
        COMMAND
            ${CMAKE_COMMAND} -DMODE=fixed_point ${common}
            "-DACTUAL=${scratch}/again.hpp" -P
            "${CMAKE_CURRENT_SOURCE_DIR}/run-case.cmake"
    )
    if(SPLIT_INLINE_CLANG)
        add_test(
            NAME split_inline.${case}.compiles
            COMMAND
                "${SPLIT_INLINE_CLANG}" -std=c++2c -fsyntax-only -Wall
                "${expected}"
        )
    endif()
    if(ARG_SAME_WORDING)
        add_test(
            NAME split_inline.${case}.same_wording
            COMMAND
                ${CMAKE_COMMAND} -DMODE=same_wording ${common}
                "-DSPECGEN=$<TARGET_FILE:specgen>" "-DACTUAL=${scratch}/wording"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/run-case.cmake"
        )
    endif()
endfunction()

# Plain mode over the shape the plan's spike covered: a constrained class
# template with a defaulted parameter, constructors with mem-initializers,
# a destructor, a conversion function, operators, static and virtual
# members, member templates, a partial and an explicit specialization, a
# nested class, hidden friends, and defaulted and deleted members.
split_inline_add_case(members)
# The same header under --move-comments: only the `//!` docblocks travel,
# `///` and `//` stay, and the `\ref` groups become `\rSec3` sections.
split_inline_add_case(members_move MOVE_COMMENTS INPUT "${CMAKE_CURRENT_SOURCE_DIR}/cases/members/input.hpp")
# The comment table of the plan, row by row.
split_inline_add_case(comments MOVE_COMMENTS)
# A class whose definition region already has sections and out-of-line
# siblings on both sides of the moved members, and one group with no section.
split_inline_add_case(sections MOVE_COMMENTS)
# What the tool refuses, each with its diagnostic.
split_inline_add_case(refusals REFUSALS)
# Corpus headers in specgen form with in-class definitions: converted with
# their docblocks, they render the same wording.
split_inline_add_case(
    corpus_inclass
    MOVE_COMMENTS
    SAME_WORDING
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_inclass.hpp"
)
split_inline_add_case(
    corpus_inclass_bodies
    MOVE_COMMENTS
    SAME_WORDING
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_inclass_bodies.hpp"
)
split_inline_add_case(
    corpus_inclass_template
    MOVE_COMMENTS
    SAME_WORDING
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_inclass_template.hpp"
)
