# tests/tidy/CMakeLists.txt                                       -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# The specgen-no-raw-loops check's own tests
# (docs/plans/no-raw-loops-tidy-plugin.md, "Testing the check"), independent
# of the tree the check guards: one freestanding probe source per rule edge,
# each pinned against an expected-diagnostics file, plus the strip-the-markers
# negative over a production file. Conditional on the same probe as the
# plugin target itself.
if(NOT BEMAN_SPECGEN_TIDY_PLUGIN_TOOLCHAIN)
    return()
endif()

set(probes
    marked_trailing
    marked_block
    unmarked_forms
    marker_above_function
    marker_separated
    paren_next_line
    macro_loop
    template_twice
)
foreach(probe IN LISTS probes)
    add_test(
        NAME tidy.no-raw-loops.${probe}
        COMMAND
            ${CMAKE_COMMAND} "-DCLANG_TIDY=${BEMAN_SPECGEN_CLANG_TIDY}"
            "-DPLUGIN=$<TARGET_FILE:beman.specgen.tidy>"
            "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/probes/${probe}.hpp"
            "-DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/probes/${probe}.expected"
            "-DACTUAL=${CMAKE_CURRENT_BINARY_DIR}/${probe}.out" -P
            "${CMAKE_CURRENT_SOURCE_DIR}/run-check.cmake"
    )
endforeach()

# The ;-list must not split the test's command line; run-strip.cmake splits
# it back on the |.
string(
    REPLACE ";"
    "|"
    _specgen_implicit_dirs
    "${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES}"
)
add_test(
    NAME tidy.no-raw-loops.strip-markers
    COMMAND
        ${CMAKE_COMMAND} "-DCLANG_TIDY=${BEMAN_SPECGEN_CLANG_TIDY}"
        "-DPLUGIN=$<TARGET_FILE:beman.specgen.tidy>"
        "-DSOURCE=${PROJECT_SOURCE_DIR}/src/beman/specgen/docblock.cpp"
        "-DINCLUDE_DIR=${PROJECT_SOURCE_DIR}/include"
        "-DIMPLICIT_DIRS=${_specgen_implicit_dirs}"
        "-DSCRATCH=${CMAKE_CURRENT_BINARY_DIR}/docblock_stripped.cpp" -P
        "${CMAKE_CURRENT_SOURCE_DIR}/run-strip.cmake"
)
unset(_specgen_implicit_dirs)
