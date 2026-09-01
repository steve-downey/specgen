# tools/specgen/CMakeLists.txt                                     -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# The driver. Its render path is clang-free, which is what lets the backend
# goldens run on every lane; the generate path arrives with the Clang tier.
add_executable(specgen)
target_sources(specgen PRIVATE main.cpp)
target_link_libraries(specgen PRIVATE beman::specgen)
target_compile_features(specgen PRIVATE cxx_std_26)

# The front end is unconditional: find_package(Clang) is REQUIRED at the top
# level, so there is no build without it — `generate` is the driver's reason
# to exist. The compile definition names the tier the source is built against.
target_link_libraries(specgen PRIVATE beman::specgen.frontend)
target_compile_definitions(specgen PRIVATE BEMAN_SPECGEN_ENABLE_CLANG)

if(BEMAN_SPECGEN_USE_MODULES)
    set_target_properties(specgen PROPERTIES CXX_MODULE_STD ON)
endif()

# The driver is the product (decision tool-not-library: this is a tool, not a
# library), so it has to reach an install tree of its own;
# `beman_install_library`'s rules place the library, the headers and the CMake
# package, and only this rule installs the binary anyone runs.
#
# The component name is the one that function derives by stripping `beman.`
# from `beman.specgen`, so `--component specgen_Runtime` reaches the binary and
# a plain `cmake --install` with no component reaches it along with the
# library's `specgen_Development`. Naming a component that no rule creates is
# silent -- `cmake --install` treats an unknown component as an empty one and
# exits 0 -- so a misspelled `--component beman.specgen` would install
# nothing without ever failing.
if(NOT CMAKE_SKIP_INSTALL_RULES)
    include(GNUInstallDirs)
    install(
        TARGETS specgen
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT specgen_Runtime
    )
endif()
