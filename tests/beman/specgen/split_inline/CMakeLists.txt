# tests/beman/specgen/split_inline/CMakeLists.txt                  -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# The split_inline rewrite's own tests: the Clang-free half, apply_edits. The
# conversions themselves are the cases under tests/split_inline, which run
# the executable.
foreach(component rewrite)
    add_executable(beman.specgen.tests.split_inline.${component})
    target_sources(
        beman.specgen.tests.split_inline.${component}
        PRIVATE ${component}.test.cpp
    )
    target_link_libraries(
        beman.specgen.tests.split_inline.${component}
        PRIVATE beman::specgen.split_inline Catch2::Catch2WithMain
    )
    catch_discover_tests(beman.specgen.tests.split_inline.${component})
endforeach()
