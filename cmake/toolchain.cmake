# cmake/toolchain.cmake                                             -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

include_guard(GLOBAL)

# The standard floor is C++26 (decision cxx26-baseline), which needs GCC 16.
# The default `cc`/`c++` on this box are older, so name GCC 16 explicitly. Set
# the compiler before including gcc-flags.cmake so its libstdc++ rpath logic can
# query this compiler.
set(CMAKE_C_COMPILER gcc-16)
set(CMAKE_CXX_COMPILER g++-16)

include("${CMAKE_CURRENT_LIST_DIR}/gcc-flags.cmake")
