# src/beman/specgen/frontend/CMakeLists.txt                        -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Tier B — the Clang front end (design plan §3). Built as part of
# every build, against pinned LLVM/Clang (decision llvm-toolchain-pin). Its sole
# product is ir::Document; everything AST- or format-touching lives here and
# nowhere else, so the LLVM version dependency stays a single replaceable seam
# (decision ir-boundary). find_package(Clang) is done once, at the top level,
# before this is added.
add_library(beman.specgen.frontend STATIC)
add_library(beman::specgen.frontend ALIAS beman.specgen.frontend)

target_sources(beman.specgen.frontend PRIVATE frontend.cpp)

target_sources(
    beman.specgen.frontend
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS "${PROJECT_SOURCE_DIR}/include"
            FILES
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/frontend.hpp"
)

target_compile_features(beman.specgen.frontend PUBLIC cxx_std_26)

# Match the LLVM build's RTTI setting (decision llvm-toolchain-pin): against a
# no-RTTI LLVM (the official release binaries), our own emitted typeinfo would
# reference base-class typeinfo the LLVM libraries never define, which links at
# -O3 only by luck of dead-stripping and fails at -O0.
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(beman.specgen.frontend PRIVATE -fno-rtti)
endif()

# LLVM/Clang headers are noisy and not warning-clean under this project's flags;
# take them as SYSTEM so -Werror stays meaningful for our own code.
target_include_directories(
    beman.specgen.frontend
    SYSTEM
    PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS}
)

# Shared clang-cpp + LLVM is sufficient for both the tooling and formatting
# surfaces; the static component libraries are unnecessary. beman::specgen
# is PUBLIC because the tier's contract type, ir::Document, comes from the core.
target_link_libraries(
    beman.specgen.frontend
    PUBLIC beman::specgen
    PRIVATE clang-cpp LLVM
)
