# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.30...4.3)

include(infra/cmake/enable-experimental-import-std.cmake)

project(
    beman.tree_algorithms
    DESCRIPTION
        "Recursive tree algorithms: fold_fix, unfold_fix, refold over fixed-point trees"
    LANGUAGES CXX
    VERSION 0.1.0
)

# [CMAKE.SKIP_TESTS]
option(
    BEMAN_TREE_ALGORITHMS_BUILD_TESTS
    "Enable building tests and test infrastructure. Default: ${PROJECT_IS_TOP_LEVEL}. Values: { ON, OFF }."
    ${PROJECT_IS_TOP_LEVEL}
)

# [CMAKE.SKIP_EXAMPLES]
option(
    BEMAN_TREE_ALGORITHMS_BUILD_EXAMPLES
    "Enable building examples. Default: ${PROJECT_IS_TOP_LEVEL}. Values: { ON, OFF }."
    ${PROJECT_IS_TOP_LEVEL}
)

# Benchmarks build with everything else so they can never rot silently.
# They are only *run* / *measured* on demand (make bench, bench-compile) in
# the Bench configuration; ordinary builds just keep them compiling.
option(
    BEMAN_TREE_ALGORITHMS_BUILD_BENCHMARKS
    "Enable building benchmarks. Default: ${PROJECT_IS_TOP_LEVEL}. Values: { ON, OFF }."
    ${PROJECT_IS_TOP_LEVEL}
)

option(
    BEMAN_TREE_ALGORITHMS_USE_MODULES
    "Provide beman.tree_algorithms as a C++ module"
    OFF
)

if(BEMAN_TREE_ALGORITHMS_USE_MODULES)
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)
endif()

configure_file(
    "${PROJECT_SOURCE_DIR}/include/beman/tree_algorithms/config_generated.hpp.in"
    "${PROJECT_BINARY_DIR}/include/beman/tree_algorithms/config_generated.hpp"
    @ONLY
)

# for find of beman_install_library and configure_build_telemetry
include(infra/cmake/beman-install-library.cmake)
include(infra/cmake/BuildTelemetryConfig.cmake)

if(BEMAN_TREE_ALGORITHMS_USE_MODULES)
    add_library(beman.tree_algorithms STATIC)
else()
    add_library(beman.tree_algorithms INTERFACE)
endif()
add_library(beman::tree_algorithms ALIAS beman.tree_algorithms)

if(BEMAN_TREE_ALGORITHMS_USE_MODULES)
    target_sources(
        beman.tree_algorithms
        PUBLIC
            FILE_SET CXX_MODULES
            FILE_SET HEADERS
                BASE_DIRS
                    "${CMAKE_CURRENT_SOURCE_DIR}/include"
                    "${CMAKE_CURRENT_BINARY_DIR}/include"
    )
    set_target_properties(beman.tree_algorithms PROPERTIES CXX_MODULE_STD ON)
    target_compile_features(beman.tree_algorithms PUBLIC cxx_std_23)
else()
    target_sources(
        beman.tree_algorithms
        PUBLIC
            FILE_SET HEADERS
                BASE_DIRS
                    "${CMAKE_CURRENT_SOURCE_DIR}/include"
                    "${CMAKE_CURRENT_BINARY_DIR}/include"
    )
    set_target_properties(
        beman.tree_algorithms
        PROPERTIES VERIFY_INTERFACE_HEADER_SETS ${PROJECT_IS_TOP_LEVEL}
    )
endif()

add_subdirectory(include/beman/tree_algorithms)

beman_install_library(beman.tree_algorithms TARGETS beman.tree_algorithms)
configure_build_telemetry()

if(BEMAN_TREE_ALGORITHMS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/beman/tree_algorithms)
endif()

if(BEMAN_TREE_ALGORITHMS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(BEMAN_TREE_ALGORITHMS_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks/beman/tree_algorithms)
endif()

# Anchored-snippet gate. The article (docs/recursive-tree-algorithms.org)
# and the paper transclude UUID-anchored regions from the targets below.
# paper_snippets_check is part of the default build (ALL), so any anchored
# file that stops compiling fails the build; it can also be built alone:
#   cmake --build <build-dir> --config <cfg> --target paper_snippets_check
set(_paper_snippet_targets
    # examples with anchored regions
    beman.tree_algorithms.examples.fixpoint_tree_example
    beman.tree_algorithms.examples.expression_algorithms
    beman.tree_algorithms.examples.binary_tree_adapt
    # test targets whose sources (and included headers) carry anchors
    beman.tree_algorithms.tests.box
    beman.tree_algorithms.tests.fix
    beman.tree_algorithms.tests.expression
    beman.tree_algorithms.tests.functors
    beman.tree_algorithms.tests.binary_tree
)
add_custom_target(paper_snippets_check ALL)
foreach(_snippet_target IN LISTS _paper_snippet_targets)
    if(TARGET ${_snippet_target})
        add_dependencies(paper_snippets_check ${_snippet_target})
    endif()
endforeach()

# Coverage
configure_file("cmake/gcovr.cfg.in" gcovr.cfg @ONLY)

add_custom_target(
    process_coverage
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running gcovr to process coverage results"
    COMMAND mkdir -p coverage
    COMMAND gcovr --config gcovr.cfg .
)
