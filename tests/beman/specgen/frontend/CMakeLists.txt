# tests/beman/specgen/frontend/CMakeLists.txt                      -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Front-end tests. They need LLVM, which every build has, and the
# parent guards before add_subdirectory(frontend).
foreach(
    component
    frontend
    collect
    document
    synopsis
    template
    attach
    markers
    aliases
    constraints
    conditional_equiv
    equiv
    expos_uses
    header_synopsis
    index
    mandates
    rsec
    diagnostics
    doxygen
)
    add_executable(beman.specgen.tests.${component})
    target_sources(
        beman.specgen.tests.${component}
        PRIVATE ${component}.test.cpp
    )
    target_link_libraries(
        beman.specgen.tests.${component}
        PRIVATE beman::specgen.frontend Catch2::Catch2WithMain
    )
    # collect.test.cpp reads the hand-curated corpus header (decision hermetic-corpus); point
    # it at the corpus directory instead of hard-coding a relative path that
    # would depend on the test's working directory.
    target_compile_definitions(
        beman.specgen.tests.${component}
        PRIVATE BEMAN_SPECGEN_CORPUS_DIR="${PROJECT_SOURCE_DIR}/tests/corpus"
    )
    catch_discover_tests(beman.specgen.tests.${component})
endforeach()
