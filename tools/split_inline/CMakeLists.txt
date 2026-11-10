# tools/split_inline/CMakeLists.txt                                -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# split_inline: in-class member definitions to out-of-line form
# (docs/plans/split-inline.md). A separate executable from the driver, by
# decision: specgen reads `//!` as markup, drops `///` and keeps `//`, and a
# converter in the same process would have to share or contradict that. What
# the two share is the front end's plumbing -- the compile-argument precedence,
# the parse, the body splice -- which is the library linked here.

add_executable(split_inline)
target_sources(split_inline PRIVATE main.cpp)
target_link_libraries(split_inline PRIVATE beman::specgen.split_inline)
target_compile_features(split_inline PRIVATE cxx_std_26)
# The same RTTI match the libraries make (src/beman/specgen/split_inline).
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(split_inline PRIVATE -fno-rtti)
endif()
target_compile_definitions(
    split_inline
    PRIVATE
        BEMAN_SPECGEN_VERSION="${PROJECT_VERSION}"
        BEMAN_SPECGEN_GIT_COMMIT="${BEMAN_SPECGEN_GIT_COMMIT}"
)

# Clang's builtin headers, found by the same rule the driver uses:
# `<executable dir>/../lib/clang/<major>` (tools/specgen/CMakeLists.txt
# explains the rule and why the link is made after each link of the
# executable). This executable lives in its own directory of the build tree,
# so it needs a link of its own; the install already places both binaries in
# one `bin/` beside one `lib/clang/`. The directory is computed here again
# rather than read from the driver's CMakeLists, whose variables are scoped
# to that directory.
set(BEMAN_SPECGEN_RESOURCE_DIR
    "${LLVM_LIBRARY_DIR}/clang/${LLVM_VERSION_MAJOR}"
)
add_custom_command(
    TARGET split_inline
    POST_BUILD
    COMMAND
        ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:split_inline>/../lib/clang"
    COMMAND
        ${CMAKE_COMMAND} -E create_symlink "${BEMAN_SPECGEN_RESOURCE_DIR}"
        "$<TARGET_FILE_DIR:split_inline>/../lib/clang/${LLVM_VERSION_MAJOR}"
    COMMENT "Linking the Clang resource directory beside split_inline"
    VERBATIM
)

# Same component as the driver, so the release archive and the wheel, which
# are two packagings of that component's install tree, carry both.
if(NOT CMAKE_SKIP_INSTALL_RULES)
    include(GNUInstallDirs)
    install(
        TARGETS split_inline
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT specgen_Runtime
    )
endif()
