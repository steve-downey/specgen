# src/beman/specgen/backend/CMakeLists.txt                         -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Serializers over the IR. These are clang-free by construction: a backend
# that needed the AST would mean the IR had lost information it should carry.
target_sources(beman.specgen PRIVATE latex.cpp mpark.cpp org.cpp)
