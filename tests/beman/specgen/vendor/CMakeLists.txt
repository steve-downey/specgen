# tests/beman/specgen/vendor/CMakeLists.txt                        -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Acceptance tests of the vendored subtrees: what specgen relies on them for,
# tested from this side. Each subtree's own tests stay in the subtree.
foreach(component recursion_schemes)
    add_executable(beman.specgen.tests.vendor.${component})
    target_sources(
        beman.specgen.tests.vendor.${component}
        PRIVATE ${component}.test.cpp
    )
    target_link_libraries(
        beman.specgen.tests.vendor.${component}
        PRIVATE beman::specgen Catch2::Catch2WithMain
    )
    if(BEMAN_SPECGEN_USE_MODULES)
        set_target_properties(
            beman.specgen.tests.vendor.${component}
            PROPERTIES CXX_MODULE_STD ON
        )
    endif()
    catch_discover_tests(beman.specgen.tests.vendor.${component})
endforeach()
