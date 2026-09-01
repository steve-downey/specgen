# src/beman/specgen/CMakeLists.txt                                 -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# The specgen core (clang-free): semantic IR, docblock grammar, lowering
# between them, and the serializers under backend/.
# These translation units make beman.specgen a compiled library.
target_sources(
    beman.specgen
    PRIVATE
        ir.cpp
        docblock.cpp
        lower.cpp
        conjuncts.cpp
        document_build.cpp
        fragments.cpp
)

add_subdirectory(backend)
add_subdirectory(validate)
