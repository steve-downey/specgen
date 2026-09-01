# tests/beman/specgen/backend/CMakeLists.txt                       -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

foreach(component common latex mpark org)
    add_executable(beman.specgen.tests.backend.${component})
    target_sources(
        beman.specgen.tests.backend.${component}
        PRIVATE ${component}.test.cpp
    )
    target_link_libraries(
        beman.specgen.tests.backend.${component}
        PRIVATE beman::specgen Catch2::Catch2WithMain
    )
    if(BEMAN_SPECGEN_USE_MODULES)
        set_target_properties(
            beman.specgen.tests.backend.${component}
            PROPERTIES CXX_MODULE_STD ON
        )
    endif()
    catch_discover_tests(beman.specgen.tests.backend.${component})
endforeach()
