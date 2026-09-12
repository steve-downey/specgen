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
# System LLVM 23 links via shared clang-cpp + LLVM, built under GCC.
# ClangConfig pulls in LLVMConfig, so LLVM_INCLUDE_DIRS and CLANG_INCLUDE_DIRS
# are both defined after this. LLVMConfig's transitive FindFFI/FindLibEdit run
# C-compiler probes, so C must be enabled first.
#
# The version request is the point of this block, not decoration. Clang's own
# ClangConfigVersion.cmake accepts only a matching major.minor, and CMake's
# config search globs `lib/cmake/clang*`, so on a box with several LLVMs
# installed side by side an unversioned find_package(Clang) silently takes the
# newest one, which is how a freshly released LLVM breaks the build. That is
# not hypothetical: 23 renamed ASTContext::getRawCommentForDeclNoCache out from
# under frontend.cpp, and the pin is what turned that into a one-line port
# instead of a morning. Asking for the major.minor the front end is written
# against makes the search pick that LLVM and report every rejected candidate
# by version when it is missing. The request outranks a -DClang_DIR= pointing
# at some other version, so moving to a new LLVM is one deliberate flag, not an
# accident of what is installed:
#   -DBEMAN_SPECGEN_LLVM_VERSION=24.1
# and -DClang_DIR=<prefix>/lib/cmake/clang still points at one installed off
# the default search path.
set(BEMAN_SPECGEN_LLVM_VERSION
    "23.1"
    CACHE STRING
    "major.minor of the LLVM/Clang the front end is built against"
)
enable_language(C)
find_package(Clang ${BEMAN_SPECGEN_LLVM_VERSION} REQUIRED CONFIG)
message(STATUS "beman.specgen: Clang front end (LLVM ${LLVM_PACKAGE_VERSION})")

# The clang-tidy plugin toolchain (docs/plans/no-raw-loops-tidy-plugin.md,
# decision llvm-toolchain-pin: the pin covers clang-tidy and any plugin built
# against it). Three probes against the *pinned* LLVM, never PATH: a plugin
# built against one LLVM's headers and loaded into another's binary fails to
# load at best and crashes at worst. The binaries ship with every install of
# the pin; the clang-tidy headers come from clang-tools-extra, which some
# distributions package separately, so the plugin target and the gate that
# uses it hang off this probe rather than assuming. The text gate needs
# nothing built, so its replacement must say when it is unavailable rather
# than silently passing — hence a STATUS line either way.
find_program(
    BEMAN_SPECGEN_CLANG_TIDY
    clang-tidy
    PATHS ${LLVM_TOOLS_BINARY_DIR}
    NO_DEFAULT_PATH
)
find_program(
    BEMAN_SPECGEN_RUN_CLANG_TIDY
    run-clang-tidy
    PATHS ${LLVM_TOOLS_BINARY_DIR}
    NO_DEFAULT_PATH
)
find_path(
    BEMAN_SPECGEN_CLANG_TIDY_INCLUDE_DIR
    clang-tidy/ClangTidyCheck.h
    PATHS ${CLANG_INCLUDE_DIRS}
    NO_DEFAULT_PATH
)
if(
    BEMAN_SPECGEN_CLANG_TIDY
    AND BEMAN_SPECGEN_RUN_CLANG_TIDY
    AND BEMAN_SPECGEN_CLANG_TIDY_INCLUDE_DIR
)
    set(BEMAN_SPECGEN_TIDY_PLUGIN_TOOLCHAIN TRUE)
    message(
        STATUS
        "beman.specgen: clang-tidy plugin toolchain (${BEMAN_SPECGEN_CLANG_TIDY}; headers under ${BEMAN_SPECGEN_CLANG_TIDY_INCLUDE_DIR})"
    )
else()
    set(BEMAN_SPECGEN_TIDY_PLUGIN_TOOLCHAIN FALSE)
    set(_specgen_tidy_missing "")
    if(NOT BEMAN_SPECGEN_CLANG_TIDY)
        list(
            APPEND _specgen_tidy_missing
            "clang-tidy in ${LLVM_TOOLS_BINARY_DIR}"
        )
    endif()
    if(NOT BEMAN_SPECGEN_RUN_CLANG_TIDY)
        list(
            APPEND _specgen_tidy_missing
            "run-clang-tidy in ${LLVM_TOOLS_BINARY_DIR}"
        )
    endif()
    if(NOT BEMAN_SPECGEN_CLANG_TIDY_INCLUDE_DIR)
        list(
            APPEND _specgen_tidy_missing
            "clang-tidy/ClangTidyCheck.h under the Clang include directories"
        )
    endif()
    list(JOIN _specgen_tidy_missing "; " _specgen_tidy_missing)
    message(
        STATUS
        "beman.specgen: clang-tidy plugin toolchain unavailable (missing: ${_specgen_tidy_missing})"
    )
    unset(_specgen_tidy_missing)
endif()

add_subdirectory(src/beman/specgen/frontend)

# The clang-tidy plugin rides the probe above, not BEMAN_SPECGEN_BUILD_TOOLS:
# it is lint infrastructure, not the driver, and its CMakeLists returns
# immediately when the probe came up empty.
add_subdirectory(tools/tidy)

if(BEMAN_SPECGEN_BUILD_TOOLS)
    add_subdirectory(tools/specgen)
endif()

if(BEMAN_SPECGEN_BUILD_TESTS)
    enable_testing()

    # The "no raw loops" gate (docs/CODING_RULES.md), as the clang-tidy pass
    # (docs/plans/no-raw-loops-tidy-plugin.md, ctest-gate):
    # tools/tidy/run-no-raw-loops.cmake filters the compile database and runs
    # the pinned run-clang-tidy with the specgen plugin, findings as errors.
    # `ctest -R style` selects it. Without the plugin toolchain the case
    # fails loudly naming what is missing, never a silent green. The
    # ;-list must not split the test's command line; the driver splits it
    # back on the |.
    if(BEMAN_SPECGEN_TIDY_PLUGIN_TOOLCHAIN)
        string(
            REPLACE ";"
            "|"
            _specgen_gate_implicit_dirs
            "${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES}"
        )
        add_test(
            NAME style.no-raw-loops
            COMMAND
                ${CMAKE_COMMAND}
                "-DRUN_CLANG_TIDY=${BEMAN_SPECGEN_RUN_CLANG_TIDY}"
                "-DCLANG_TIDY=${BEMAN_SPECGEN_CLANG_TIDY}"
                "-DPLUGIN=$<TARGET_FILE:beman.specgen.tidy>"
                "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
                "-DIMPLICIT_DIRS=${_specgen_gate_implicit_dirs}" -P
                "${PROJECT_SOURCE_DIR}/tools/tidy/run-no-raw-loops.cmake"
        )
        unset(_specgen_gate_implicit_dirs)
    else()
        add_test(
            NAME style.no-raw-loops
            COMMAND
                ${CMAKE_COMMAND} -P
                "${PROJECT_SOURCE_DIR}/tools/tidy/no-raw-loops-unavailable.cmake"
        )
    endif()

    # The plugin-load smoke (docs/plans/no-raw-loops-tidy-plugin.md, stage
    # tidy-plugin-target): the pinned clang-tidy dlopens the plugin and lists
    # the specgen checks, which proves registration reached the registry the
    # binary reads — the exact seam a mismatched toolchain breaks.
    if(BEMAN_SPECGEN_TIDY_PLUGIN_TOOLCHAIN)
        add_test(
            NAME style.tidy-plugin-loads
            COMMAND
                ${BEMAN_SPECGEN_CLANG_TIDY} --load
                $<TARGET_FILE:beman.specgen.tidy> --list-checks
                --checks=-*,specgen-*
        )
        set_tests_properties(
            style.tidy-plugin-loads
            PROPERTIES PASS_REGULAR_EXPRESSION "specgen-no-raw-loops"
        )
    endif()

    add_subdirectory(tests/beman/specgen)
    # The specgen-no-raw-loops check's own probe tests; the file returns
    # immediately when the plugin toolchain probe came up empty.
    add_subdirectory(tests/tidy)
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
