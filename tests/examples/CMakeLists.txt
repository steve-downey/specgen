# tests/examples/CMakeLists.txt                                    -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The checks behind docs/examples.org. Every output the document shows
# is a file some script under examples/cli/ actually wrote, and every one of
# those files is covered here in one of two ways:
#
#   examples.parity.*  The captured file has a golden twin. Compare the two
#                      checked-in files directly -- no binary runs, so these
#                      register on every lane. This is transitive proof rather
#                      than a second opinion: golden.<case> already pins the
#                      golden to what specgen printed, so captured == golden
#                      says the captured file is real tool output.
#
#   examples.rerun.*   The captured file has no golden twin (--help texts, the
#                      dump-decls stream, the piped render, the clean-validate
#                      render). Re-run the script with this build's driver into
#                      a scratch directory and compare everything it wrote.
#
# The driver is built only where the front end is, so every rerun
# case lives on the Tier B lanes. The parity cases do not: they compare two
# checked-in files and need no driver at all, which is why a portable lane that
# builds no CLI still checks even the *generated* captures.
set(EXAMPLES_CLI "${PROJECT_SOURCE_DIR}/examples/cli")
set(EXAMPLES_OUT "${EXAMPLES_CLI}/output")
set(GOLDEN_DIR "${PROJECT_SOURCE_DIR}/tests/golden")

# specgen_add_example_parity(<name> CAPTURED <rel> GOLDEN <rel>)
#
# One captured file against one golden file, byte for byte.
function(specgen_add_example_parity name)
    cmake_parse_arguments(ARG "" "CAPTURED;GOLDEN" "" ${ARGN})
    add_test(
        NAME examples.parity.${name}
        COMMAND
            "${CMAKE_COMMAND}" -E compare_files
            "${EXAMPLES_OUT}/${ARG_CAPTURED}" "${GOLDEN_DIR}/${ARG_GOLDEN}"
    )
endfunction()

# specgen_add_example_tree(<name> CAPTURED <rel> GOLDEN <rel>)
#
# The same, where the golden twin is a directory of fragments.
function(specgen_add_example_tree name)
    cmake_parse_arguments(ARG "" "CAPTURED;GOLDEN" "" ${ARGN})
    add_test(
        NAME examples.parity.${name}
        COMMAND
            "${CMAKE_COMMAND}" -DMODE=tree
            "-DACTUAL=${EXAMPLES_OUT}/${ARG_CAPTURED}"
            "-DEXPECTED=${GOLDEN_DIR}/${ARG_GOLDEN}" -P
            "${CMAKE_CURRENT_SOURCE_DIR}/run-example.cmake"
    )
endfunction()

# specgen_add_example_rerun(<section>)
#
# Re-run examples/cli/<section>.sh and compare everything it writes. Every such
# case needs the driver, and the driver exists only where the front
# end does -- so there is no per-case tier flag, just this one guard.
function(specgen_add_example_rerun section)
    if(NOT TARGET specgen)
        return()
    endif()
    add_test(
        NAME examples.rerun.${section}
        COMMAND
            "${CMAKE_COMMAND}" -DMODE=rerun "-DSPECGEN=$<TARGET_FILE:specgen>"
            "-DSCRIPT=${EXAMPLES_CLI}/${section}.sh" "-DSECTION=${section}"
            "-DCAPTURED=${EXAMPLES_OUT}/${section}"
            "-DSCRATCH=${CMAKE_CURRENT_BINARY_DIR}/scratch/${section}" -P
            "${CMAKE_CURRENT_SOURCE_DIR}/run-example.cmake"
    )
endfunction()

# --- Parity: captured output against the golden that pins the same bytes ----

specgen_add_example_parity(
    widget_ir
    CAPTURED 20-widget-ir/widget.json
    GOLDEN widget_skeleton/expected.json
)

# One IR, three backends -- the same triple the backend goldens render.
specgen_add_example_parity(
    value_or_latex
    CAPTURED 30-render-backends/value_or.tex
    GOLDEN value_or/expected.tex
)
specgen_add_example_parity(
    value_or_mpark
    CAPTURED 30-render-backends/value_or.md
    GOLDEN value_or/expected.md
)
specgen_add_example_parity(
    value_or_org
    CAPTURED 30-render-backends/value_or.org
    GOLDEN value_or/expected.org
)

specgen_add_example_parity(
    paper_mode
    CAPTURED 40-paper-mode/paper-mode.md
    GOLDEN paper_mode/expected.md
)

# The fragment captures: a manifest file and a directory of fragments per
# backend, from the same split the fragments goldens pin.
foreach(backend latex mpark org)
    specgen_add_example_parity(
        fragments_${backend}_manifest
        CAPTURED 50-fragments/${backend}/manifest
        GOLDEN fragments/expected.${backend}.manifest
    )
    specgen_add_example_tree(
        fragments_${backend}
        CAPTURED 50-fragments/${backend}/wording
        GOLDEN fragments/expected.${backend}
    )
endforeach()

specgen_add_example_parity(
    validate_coverage
    CAPTURED 60-validate/coverage.diag
    GOLDEN validate_coverage/expected.diag
)

specgen_add_example_parity(
    diagnostics
    CAPTURED 65-diagnostics/diagnostics.diag
    GOLDEN diagnostics/expected.diag
)

# Both routes to one parse are compared against the same golden, which is the
# EXPECTED_DIR trick golden.include_path_db already uses: a difference between
# them would have to be a real difference rather than two files that merely
# agree with each other.
specgen_add_example_parity(
    include_path
    CAPTURED 70-include-path/include-path.json
    GOLDEN include_path/expected.json
)
specgen_add_example_parity(
    include_path_db
    CAPTURED 70-include-path/include-path-from-db.json
    GOLDEN include_path/expected.json
)

specgen_add_example_parity(
    optional_ir
    CAPTURED 90-optional/optional.json
    GOLDEN optional/expected.json
)
specgen_add_example_parity(
    optional_mpark
    CAPTURED 90-optional/optional.md
    GOLDEN optional/expected.md
)

# --- Rerun: everything each script writes, against this build's driver -------

specgen_add_example_rerun(10-help)
specgen_add_example_rerun(20-widget-ir)
specgen_add_example_rerun(30-render-backends)
specgen_add_example_rerun(40-paper-mode)
specgen_add_example_rerun(50-fragments)
specgen_add_example_rerun(60-validate)
specgen_add_example_rerun(65-diagnostics)
specgen_add_example_rerun(70-include-path)
specgen_add_example_rerun(80-dump-decls)
specgen_add_example_rerun(90-optional)
