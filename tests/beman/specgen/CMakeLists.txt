# tests/beman/specgen/CMakeLists.txt                               -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Catch2 is built as a subdirectory of this one, so it would inherit the
# project's switch errors; they are for our code.
get_directory_property(_specgen_compile_options COMPILE_OPTIONS)
set_directory_properties(PROPERTIES COMPILE_OPTIONS "")
find_package(Catch2 3 REQUIRED)
set_directory_properties(
    PROPERTIES COMPILE_OPTIONS "${_specgen_compile_options}"
)
unset(_specgen_compile_options)
list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
include(Catch)

foreach(
    component
    ir
    ir_fold
    ir_json
    docblock
    markers
    rsec
    lower
    conjuncts
    depfile
    diagnostic
    document_build
    fragments
    span_sentinels
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
add_subdirectory(json)
add_subdirectory(parse)
add_subdirectory(validate)
add_subdirectory(vendor)

# Tier B tests: the Clang front end's own.
add_subdirectory(frontend)
