# tests/beman/specgen/foundation/CMakeLists.txt                    -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Foundation idioms: the ported headers and the vendored recursion verbs.
# recursion_schemes.test.cpp holds the fold_with
# identity/composition law tests on a toy rose tree. Catch2 and its module path
# are set up by the parent tests CMakeLists.
foreach(
    component
    overloaded
    enumeration
    fold_left_short
    monoid
    traverse
    recursion_schemes
    json_writer
    json_descriptor
)
    add_executable(beman.specgen.tests.foundation.${component})
    target_sources(
        beman.specgen.tests.foundation.${component}
        PRIVATE ${component}.test.cpp
    )
    target_link_libraries(
        beman.specgen.tests.foundation.${component}
        PRIVATE beman::specgen Catch2::Catch2WithMain
    )
    if(BEMAN_SPECGEN_USE_MODULES)
        set_target_properties(
            beman.specgen.tests.foundation.${component}
            PROPERTIES CXX_MODULE_STD ON
        )
    endif()
    catch_discover_tests(beman.specgen.tests.foundation.${component})
endforeach()

add_subdirectory(parse)

# A table that leaves an enumerator out must not compile, and the diagnostic
# names the enumerator. The probe is built on demand, by the test, rather than
# with `all`.
add_executable(
    beman.specgen.probe.enumeration_missing
    EXCLUDE_FROM_ALL
    enumeration_missing.probe.cpp
)
target_link_libraries(beman.specgen.probe.enumeration_missing PRIVATE beman::specgen)
add_test(
    NAME foundation.enumeration.missing-case
    COMMAND
        ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --config $<CONFIG>
        --target beman.specgen.probe.enumeration_missing
)
set_tests_properties(
    foundation.enumeration.missing-case
    PROPERTIES PASS_REGULAR_EXPRESSION "enumerator_handled_once<Color::Blue, 0>"
)
