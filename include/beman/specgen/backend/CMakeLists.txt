# include/beman/specgen/backend/CMakeLists.txt                     -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

target_sources(
    beman.specgen
    PUBLIC FILE_SET HEADERS FILES common.hpp latex.hpp mpark.hpp org.hpp
)
