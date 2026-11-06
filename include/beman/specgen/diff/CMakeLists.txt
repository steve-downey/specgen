# include/beman/specgen/diff/CMakeLists.txt                        -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# The wording diff's sequence core (docs/plans/wording-diff.md): header-only
# and clang-free, like parse/.
target_sources(beman.specgen PUBLIC FILE_SET HEADERS FILES sequence.hpp)
