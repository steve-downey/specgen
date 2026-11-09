# src/beman/specgen/split_inline/CMakeLists.txt                    -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# The split_inline rewrite (docs/plans/split-inline.md): in-class member
# definitions to out-of-line form, as a list of edits over a parsed header.
# Its own library, like the front end it builds on, so the executable under
# tools/split_inline and the tests under tests/beman/specgen/split_inline link
# one thing. Reaches Clang, so it is outside the module umbrella as the front
# end is.
add_library(beman.specgen.split_inline STATIC)
add_library(beman::specgen.split_inline ALIAS beman.specgen.split_inline)

target_sources(beman.specgen.split_inline PRIVATE rewrite.cpp)
target_sources(
    beman.specgen.split_inline
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS "${PROJECT_SOURCE_DIR}/include"
            FILES
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/split_inline/rewrite.hpp"
)
target_link_libraries(
    beman.specgen.split_inline
    PUBLIC beman::specgen.frontend beman::specgen
)
target_compile_features(beman.specgen.split_inline PUBLIC cxx_std_26)
