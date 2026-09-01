# tests/beman/specgen/validate/CMakeLists.txt                      -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

foreach(component validate)
    add_executable(beman.specgen.tests.validate.${component})
    target_sources(
        beman.specgen.tests.validate.${component}
        PRIVATE ${component}.test.cpp
    )
    target_link_libraries(
        beman.specgen.tests.validate.${component}
        PRIVATE beman::specgen Catch2::Catch2WithMain
    )
    if(BEMAN_SPECGEN_USE_MODULES)
        set_target_properties(
            beman.specgen.tests.validate.${component}
            PROPERTIES CXX_MODULE_STD ON
        )
    endif()
    catch_discover_tests(beman.specgen.tests.validate.${component})
endforeach()
