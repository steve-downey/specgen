# include/beman/specgen/json/CMakeLists.txt                        -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

target_sources(
    beman.specgen
    PUBLIC FILE_SET HEADERS FILES descriptor.hpp reader.hpp writer.hpp
)
