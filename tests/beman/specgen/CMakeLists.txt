# tests/beman/specgen/CMakeLists.txt                               -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

find_package(Catch2 3 REQUIRED)
list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
include(Catch)

foreach(
    component
    ir
    ir_fold
    docblock
    lower
    conjuncts
    document_build
    fragments
)
    add_executable(beman.specgen.tests.${component})
    target_sources(
        beman.specgen.tests.${component}
        PRIVATE ${component}.test.cpp
    )
    target_link_libraries(
        beman.specgen.tests.${component}
        PRIVATE beman::specgen Catch2::Catch2WithMain
    )
    if(BEMAN_SPECGEN_USE_MODULES)
        set_target_properties(
            beman.specgen.tests.${component}
            PROPERTIES CXX_MODULE_STD ON
        )
    endif()
    catch_discover_tests(beman.specgen.tests.${component})
endforeach()

add_subdirectory(backend)
add_subdirectory(foundation)
add_subdirectory(validate)

# Tier B tests, only when the front end is built.
add_subdirectory(frontend)
