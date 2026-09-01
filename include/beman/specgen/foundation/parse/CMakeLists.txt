# include/beman/specgen/foundation/parse/CMakeLists.txt            -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# The runtime-ized parser-combinator core (decision parser-combinators):
# header-only, clang-free, and a dependency of both build configurations
# (parse_rsec and the docblock scanners are built on it).
target_sources(
    beman.specgen
    PUBLIC FILE_SET HEADERS FILES cursor.hpp parser.hpp
)
