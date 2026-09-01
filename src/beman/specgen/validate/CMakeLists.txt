# src/beman/specgen/validate/CMakeLists.txt                        -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Phase-4 validators over the IR. Clang-free by construction, like the
# backends: a validator that needed the AST would mean the IR had lost
# information it should carry.
target_sources(beman.specgen PRIVATE validate.cpp)
