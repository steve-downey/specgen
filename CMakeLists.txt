# CMakeLists.txt                                                   -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.30...4.3)

include(infra/cmake/enable-experimental-import-std.cmake)

project(
    beman.specgen
    DESCRIPTION "Standard-library description wording generator"
    LANGUAGES CXX
    VERSION 0.1.0
)

# [CMAKE.SKIP_TESTS]
option(
    BEMAN_SPECGEN_BUILD_TESTS
    "Enable building tests and test infrastructure. Default: ${PROJECT_IS_TOP_LEVEL}. Values: { ON, OFF }."
    ${PROJECT_IS_TOP_LEVEL}
)

# [CMAKE.SKIP_EXAMPLES]
option(
    BEMAN_SPECGEN_BUILD_EXAMPLES
    "Enable building examples. Default: ${PROJECT_IS_TOP_LEVEL}. Values: { ON, OFF }."
    ${PROJECT_IS_TOP_LEVEL}
)

# [CMAKE.SKIP_TOOLS]
option(
    BEMAN_SPECGEN_BUILD_TOOLS
    "Enable building the specgen driver. Default: ${PROJECT_IS_TOP_LEVEL}. Values: { ON, OFF }."
    ${PROJECT_IS_TOP_LEVEL}
)

option(BEMAN_SPECGEN_USE_MODULES "Provide beman.specgen as a C++ module" OFF)

if(BEMAN_SPECGEN_USE_MODULES)
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)
endif()

configure_file(
    "${PROJECT_SOURCE_DIR}/include/beman/specgen/config_generated.hpp.in"
    "${PROJECT_BINARY_DIR}/include/beman/specgen/config_generated.hpp"
    @ONLY
)

# for find of beman_install_library and configure_build_telemetry
include(infra/cmake/beman-install-library.cmake)
include(infra/cmake/BuildTelemetryConfig.cmake)

# beman.specgen is a compiled library: the specgen core (semantic IR + docblock
# grammar) provides real translation units, so the target is STATIC in every
# configuration. Sources are attached under src/beman/specgen.
add_library(beman.specgen STATIC)
add_library(beman::specgen ALIAS beman.specgen)

target_compile_features(beman.specgen PUBLIC cxx_std_26)

# Vendored recursion verbs (decision subtree-consumption): fold_with/unfold_with
# and the Fix verbs, consumed header-only from the subtree include path, explicit-
# parameter tier only (decision no-typeclass-objects). Taken SYSTEM so -Werror stays meaningful for our
# own code; local edits to vendored files are forbidden (friction is logged in
# include/beman/specgen/foundation/DIVERGENCES.md and pushed upstream instead).
target_include_directories(
    beman.specgen
    SYSTEM
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/vendor/tree_algorithms/include>
)

if(BEMAN_SPECGEN_USE_MODULES)
    target_sources(
        beman.specgen
        PUBLIC
            FILE_SET CXX_MODULES
            FILE_SET HEADERS
                BASE_DIRS
                    "${CMAKE_CURRENT_SOURCE_DIR}/include"
                    "${CMAKE_CURRENT_BINARY_DIR}/include"
    )
    set_target_properties(beman.specgen PROPERTIES CXX_MODULE_STD ON)
else()
    target_sources(
        beman.specgen
        PUBLIC
            FILE_SET HEADERS
                BASE_DIRS
                    "${CMAKE_CURRENT_SOURCE_DIR}/include"
                    "${CMAKE_CURRENT_BINARY_DIR}/include"
    )
endif()

set_target_properties(
    beman.specgen
    PROPERTIES VERIFY_INTERFACE_HEADER_SETS ${PROJECT_IS_TOP_LEVEL}
)

add_subdirectory(include/beman/specgen)
add_subdirectory(src/beman/specgen)

beman_install_library(beman.specgen TARGETS beman.specgen)
configure_build_telemetry()

# The Clang front end, required rather than optional (decision llvm-toolchain-pin).
# System LLVM 22 links via shared clang-cpp + LLVM, built under GCC.
# ClangConfig pulls in LLVMConfig, so LLVM_INCLUDE_DIRS and CLANG_INCLUDE_DIRS
# are both defined after this. LLVMConfig's transitive FindFFI/FindLibEdit run
# C-compiler probes, so C must be enabled first.
#
# The version request is the point of this block, not decoration. Clang's own
# ClangConfigVersion.cmake accepts only a matching major.minor, and CMake's
# config search globs `lib/cmake/clang*`, so on a box with several LLVMs
# installed side by side an unversioned find_package(Clang) silently takes the
# newest one, which is how a freshly released LLVM breaks the build (23 renamed
# ASTContext::getRawCommentForDeclNoCache out from under frontend.cpp). Asking
# for the major.minor the front end is written against makes the search pick
# that LLVM and report every rejected candidate by version when it is missing.
# The request outranks a -DClang_DIR= pointing at some other version, so moving
# to a new LLVM is one deliberate flag, not an accident of what is installed:
#   -DBEMAN_SPECGEN_LLVM_VERSION=23.0
# and -DClang_DIR=<prefix>/lib/cmake/clang still points at one installed off
# the default search path.
set(BEMAN_SPECGEN_LLVM_VERSION
    "22.1"
    CACHE STRING
    "major.minor of the LLVM/Clang the front end is built against"
)
enable_language(C)
find_package(Clang ${BEMAN_SPECGEN_LLVM_VERSION} REQUIRED CONFIG)
message(STATUS "beman.specgen: Clang front end (LLVM ${LLVM_PACKAGE_VERSION})")
add_subdirectory(src/beman/specgen/frontend)

if(BEMAN_SPECGEN_BUILD_TOOLS)
    add_subdirectory(tools/specgen)
endif()

if(BEMAN_SPECGEN_BUILD_TESTS)
    enable_testing()

    # The "no raw loops" gate (docs/CODING_RULES.md):
    # a style check over source text, needing no target built, so it is
    # registered here rather than beside the golden cases below (which need the
    # driver). `ctest -R style` selects
    # it; tools/check-raw-loops.cmake's top-of-file comment is the interface.
    add_test(
        NAME style.no-raw-loops
        COMMAND
            ${CMAKE_COMMAND} -P
            "${PROJECT_SOURCE_DIR}/tools/check-raw-loops.cmake"
    )

    add_subdirectory(tests/beman/specgen)
    # Golden and example-rerun cases run through the driver, so they are
    # registered only where it is built. The example parity cases are not: they
    # compare two checked-in files, so they run even with BUILD_TOOLS off.
    if(TARGET specgen)
        add_subdirectory(tests/golden)
    endif()
    add_subdirectory(tests/examples)
endif()

if(BEMAN_SPECGEN_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Coverage
configure_file("cmake/gcovr.cfg.in" gcovr.cfg @ONLY)

add_custom_target(
    process_coverage
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running gcovr to process coverage results"
    COMMAND mkdir -p coverage
    COMMAND gcovr --config gcovr.cfg .
)
