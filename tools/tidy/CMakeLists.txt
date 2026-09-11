# tools/tidy/CMakeLists.txt                                       -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# The specgen clang-tidy plugin (docs/plans/no-raw-loops-tidy-plugin.md),
# conditional on the top-level toolchain probe: without the pinned LLVM's
# clang-tidy and the clang-tools-extra headers there is nothing to build
# against, and the probe's STATUS line has already said so.
#
# A MODULE library, not SHARED: nothing links against it, it is only ever
# dlopened by clang-tidy --load. It takes *no* target_link_libraries at all —
# in particular never libclangTidy.a, which would give it a private copy of
# the module registry (see the registrar comment in specgen_tidy_module.cpp);
# its undefined symbols resolve at load time against the clang-tidy binary.
if(NOT BEMAN_SPECGEN_TIDY_PLUGIN_TOOLCHAIN)
    return()
endif()

add_library(beman.specgen.tidy MODULE specgen_tidy_module.cpp)

target_compile_features(beman.specgen.tidy PRIVATE cxx_std_26)

# Match the LLVM build's RTTI setting, exactly as the front end does
# (decision llvm-toolchain-pin): the official release binaries build LLVM
# without RTTI, and our own emitted typeinfo would reference base-class
# typeinfo the loading binary never defines.
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(beman.specgen.tidy PRIVATE -fno-rtti)
endif()

# The clang-tidy headers live where the probe found them, beside Clang's
# elsewhere; all three are noisy under this project's flags and taken as
# SYSTEM so -Werror stays meaningful for our own code.
target_include_directories(
    beman.specgen.tidy
    SYSTEM
    PRIVATE
        ${BEMAN_SPECGEN_CLANG_TIDY_INCLUDE_DIR}
        ${LLVM_INCLUDE_DIRS}
        ${CLANG_INCLUDE_DIRS}
)
