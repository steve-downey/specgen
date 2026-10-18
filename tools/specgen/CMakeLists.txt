# tools/specgen/CMakeLists.txt                                     -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# The driver. Its render path runs no compiler, which is what lets the backend
# goldens run on checked-in IR alone; generate parses through the front end.
add_executable(specgen)
target_sources(specgen PRIVATE main.cpp)
target_link_libraries(specgen PRIVATE beman::specgen)
target_compile_features(specgen PRIVATE cxx_std_26)
target_compile_definitions(
    specgen
    PRIVATE
        BEMAN_SPECGEN_VERSION="${PROJECT_VERSION}"
        BEMAN_SPECGEN_GIT_COMMIT="${BEMAN_SPECGEN_GIT_COMMIT}"
)

# The front end is unconditional: find_package(Clang) is REQUIRED at the top
# level, so there is no build without it — `generate` is the driver's reason
# to exist.
target_link_libraries(specgen PRIVATE beman::specgen.frontend)

# Clang's builtin headers (<stddef.h>, <stdarg.h>, ...), which the front end
# finds at `<driver dir>/../lib/clang/<major>` -- Clang's own rule, asked of the
# executable at run time (frontend.cpp, bundled_resource_dir_args). Both trees
# lay that directory out, so the driver never depends on a distribution's
# baked-in fallback for it: the build tree links the pinned LLVM's own resource
# directory into place, and the install copies its headers. The path is the
# literal `lib`, never CMAKE_INSTALL_LIBDIR: Clang's rule says `lib`, and
# GNUInstallDirs says `lib64` on the EL9 the release binary is built on.
set(BEMAN_SPECGEN_RESOURCE_DIR
    "${LLVM_LIBRARY_DIR}/clang/${LLVM_VERSION_MAJOR}"
)
if(NOT IS_DIRECTORY "${BEMAN_SPECGEN_RESOURCE_DIR}/include")
    message(
        FATAL_ERROR
        "beman.specgen: no Clang resource directory at ${BEMAN_SPECGEN_RESOURCE_DIR}"
    )
endif()
# The link goes beside the executable itself, wherever the generator put it:
# a multi-config generator builds `specgen/<Config>/specgen`, so a fixed
# `${CMAKE_CURRENT_BINARY_DIR}/..` missed by one directory there and Clang got
# a relative `-resource-dir` that resolved against the user's working
# directory (issue #139). An OUTPUT rule cannot name `$<TARGET_FILE_DIR>`, so
# the link is made after each link of the driver; create_symlink replaces one
# already there.
add_custom_command(
    TARGET specgen
    POST_BUILD
    COMMAND
        ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:specgen>/../lib/clang"
    COMMAND
        ${CMAKE_COMMAND} -E create_symlink "${BEMAN_SPECGEN_RESOURCE_DIR}"
        "$<TARGET_FILE_DIR:specgen>/../lib/clang/${LLVM_VERSION_MAJOR}"
    COMMENT "Linking the Clang resource directory beside the driver"
    VERBATIM
)

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
    # Only `include/`: the resource directory's `lib/` holds sanitizer and
    # builtins runtimes for a linker specgen never runs.
    install(
        DIRECTORY "${BEMAN_SPECGEN_RESOURCE_DIR}/include"
        DESTINATION "lib/clang/${LLVM_VERSION_MAJOR}"
        COMPONENT specgen_Runtime
    )
endif()
