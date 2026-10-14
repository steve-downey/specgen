# packaging/pypi/CMakeLists.txt                                    -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Copies an already-built, already-staged specgen into the wheel; see
# pyproject.toml. The tree keeps its shape under beman_specgen/data/, so the
# driver still finds Clang's builtin headers at <its dir>/../lib/clang/<major>.

cmake_minimum_required(VERSION 3.30...4.3)
project(beman_specgen_wheel LANGUAGES NONE)

set(BEMAN_SPECGEN_STAGE
    ""
    CACHE PATH
    "A staged release tree, as tools/release/stage.sh writes it"
)
if(NOT IS_DIRECTORY "${BEMAN_SPECGEN_STAGE}/bin")
    message(
        FATAL_ERROR
        "beman-specgen wheel: BEMAN_SPECGEN_STAGE ('${BEMAN_SPECGEN_STAGE}') is not a staged tree"
    )
endif()

install(
    PROGRAMS "${BEMAN_SPECGEN_STAGE}/bin/specgen"
    DESTINATION beman_specgen/data/bin
)
install(DIRECTORY "${BEMAN_SPECGEN_STAGE}/lib" DESTINATION beman_specgen/data)
