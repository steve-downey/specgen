# installtest/CMakeLists.txt                                       -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# A standalone consumer of the *install tree*, not of the build tree. Nothing
# here refers to the parent project, which is the point: it fails if anything
# the install promises is missing -- a header left out of the FILE_SET, a
# target not exported, or the driver not installed at all.
#
# The Makefile's `testinstall` target supplies both of the cache entries this
# needs: CMAKE_PREFIX_PATH pointing at the install prefix, and the repository's
# own toolchain file, since the installed library is built by GCC 16 against
# C++26 and a consumer has to match it.
cmake_minimum_required(VERSION 3.30...4.3)
project(beman.specgen.installtest LANGUAGES CXX)

find_package(beman.specgen REQUIRED)

# The library half: the exported target compiles, links, and runs.
add_executable(consumer consumer.cpp)
target_link_libraries(consumer PRIVATE beman::specgen)
target_compile_features(consumer PRIVATE cxx_std_26)

# The driver half. specgen is a tool before it is a library (decision
# tool-not-library), so an install that ships the library and not the binary is
# a broken install, and REQUIRED makes that a configure-time failure rather
# than a silently skipped test. The search is pinned to the install prefix with
# NO_DEFAULT_PATH on purpose: a specgen already on PATH would otherwise satisfy
# this test without the install having produced anything at all.
find_program(
    SPECGEN_INSTALLED_DRIVER
    NAMES specgen
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES bin
    NO_DEFAULT_PATH
    REQUIRED
)

enable_testing()
add_test(NAME installtest.library COMMAND consumer)
add_test(
    NAME installtest.driver
    COMMAND "${SPECGEN_INSTALLED_DRIVER}" --version
)
