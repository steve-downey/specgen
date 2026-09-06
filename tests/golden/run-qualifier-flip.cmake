# tests/golden/run-qualifier-flip.cmake                            -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The east-const sibling of a generate-mode golden (issue #39). The draft
# writes `const T&`; a library may write `T const&`, and which one it writes is
# not supposed to reach the wording. This runs the header through clang-format
# with the *opposite* qualifier order and requires `generate --emit-ir` to
# produce the checked-in IR byte for byte.
#
# The flipped header is generated here rather than checked in, which is the
# whole trick: `tests/corpus/*.hpp` are clang-format-controlled, so an
# east-const spelling committed to one would be rewritten west by the next
# `make lint` and the case would quietly stop testing anything. Generating it
# at test time also means the pair can never drift -- there is one header.
#
# `BasedOnStyle: InheritParentConfig` with `--assume-filename` pointed at the
# real header resolves the repository's own .clang-format, so the flip changes
# the qualifier order and nothing else; the file keeps its line count, and the
# two parses differ in exactly the property under test.
#
# HEADER's directory goes on the include path because the flipped copy lives in
# the build tree: a quoted `#include "support/..."` resolves against the
# including file's own directory first, which is no longer the corpus.

if(NOT CLANG_FORMAT OR NOT SPECGEN OR NOT HEADER OR NOT EXPECTED OR NOT ACTUAL)
    message(
        FATAL_ERROR
        "run-qualifier-flip.cmake: CLANG_FORMAT, SPECGEN, HEADER, EXPECTED, and ACTUAL are required"
    )
endif()

get_filename_component(header_dir "${HEADER}" DIRECTORY)
get_filename_component(header_name "${HEADER}" NAME)
set(flipped "${ACTUAL}.dir/${header_name}")
file(MAKE_DIRECTORY "${ACTUAL}.dir")

execute_process(
    COMMAND
        "${CLANG_FORMAT}" "--assume-filename=${HEADER}"
        "--style={BasedOnStyle: InheritParentConfig, QualifierAlignment: Right}"
    INPUT_FILE "${HEADER}"
    OUTPUT_FILE "${flipped}"
    RESULT_VARIABLE format_result
    ERROR_VARIABLE format_error
)
if(NOT format_result EQUAL 0)
    message(FATAL_ERROR "clang-format failed on ${HEADER}: ${format_error}")
endif()

# A header with no qualifier to move is not a failure -- most of the corpus is
# in that state -- but it is not a test either, so say so rather than passing
# silently and leaving the case looking like coverage it is not.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${HEADER}" "${flipped}"
    RESULT_VARIABLE unchanged
    OUTPUT_QUIET
    ERROR_QUIET
)
if(unchanged EQUAL 0)
    message(
        STATUS
        "no qualifier to flip in ${header_name}; the parses are trivially equal"
    )
endif()

set(generate_args
    generate
    --emit-ir
    "${flipped}"
    --no-compile-commands
    -o
    "${ACTUAL}"
)
set(tail_args -I "${header_dir}")
if(EXTRA_ARGS)
    separate_arguments(extra NATIVE_COMMAND "${EXTRA_ARGS}")
    list(APPEND tail_args ${extra})
endif()

execute_process(
    COMMAND "${SPECGEN}" ${generate_args} -- ${tail_args}
    RESULT_VARIABLE generate_result
    ERROR_VARIABLE generate_error
)
if(NOT generate_result EQUAL 0)
    message(
        FATAL_ERROR
        "specgen generate failed on the qualifier-flipped ${header_name}: ${generate_error}"
    )
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${EXPECTED}" "${ACTUAL}"
    RESULT_VARIABLE differs
    OUTPUT_QUIET
    ERROR_QUIET
)
if(NOT differs EQUAL 0)
    message(
        FATAL_ERROR
        "qualifier spelling reached the wording: ${header_name} written east-const does not "
        "produce the checked-in IR.\n  expected: ${EXPECTED}\n  actual:   ${ACTUAL}\n"
        "  flipped header: ${flipped}"
    )
endif()
