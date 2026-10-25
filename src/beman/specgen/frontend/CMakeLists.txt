# src/beman/specgen/frontend/CMakeLists.txt                        -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Tier B — the Clang front end (design plan §3). Built as part of
# every build, against pinned LLVM/Clang (decision llvm-toolchain-pin). Its sole
# product is ir::Document; everything AST- or format-touching lives here and
# nowhere else, so the LLVM version dependency stays a single replaceable seam
# (decision ir-boundary). find_package(Clang) is done once, at the top level,
# before this is added.
add_library(beman.specgen.frontend STATIC)
add_library(beman::specgen.frontend ALIAS beman.specgen.frontend)

target_sources(
    beman.specgen.frontend
    PRIVATE
        attach.cpp
        body_uses.cpp
        class_head_mandates.cpp
        compile_args.cpp
        constraints.cpp
        coverage_roster.cpp
        crash_handler.cpp
        decl_projections.cpp
        declaration_masks.cpp
        directive_sets.cpp
        docblock_lookup.cpp
        document_files.cpp
        draft_format.cpp
        expos_spellings.cpp
        file_views.cpp
        foreign_names.cpp
        frontend.cpp
        inclass_items.cpp
        interleave.cpp
        itemdecl.cpp
        mandates.cpp
        merged_twins.cpp
        parse_header.cpp
        qualifiers.cpp
        record_members.cpp
        requires_clauses.cpp
        smoke_check.cpp
        synopsis_extraction.cpp
)

target_sources(
    beman.specgen.frontend
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS "${PROJECT_SOURCE_DIR}/include"
            FILES
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/attach.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/body_uses.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/build_failure.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/class_head_mandates.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/compile_args.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/constraints.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/coverage_roster.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/crash_handler.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/decl_projections.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/declaration_masks.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/directive_sets.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/docblock_lookup.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/document_files.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/draft_format.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/expos_spellings.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/file_views.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/foreign_names.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/frontend.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/inclass_items.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/interleave.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/itemdecl.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/mandates.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/merged_twins.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/parse_header.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/qualifiers.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/record_members.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/requires_clauses.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/smoke_check.hpp"
                "${PROJECT_SOURCE_DIR}/include/beman/specgen/frontend/synopsis_extraction.hpp"
)

target_compile_features(beman.specgen.frontend PUBLIC cxx_std_26)

# Match the LLVM build's RTTI setting (decision llvm-toolchain-pin): against a
# no-RTTI LLVM (the official release binaries), our own emitted typeinfo would
# reference base-class typeinfo the LLVM libraries never define, which links at
# -O3 only by luck of dead-stripping and fails at -O0.
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(beman.specgen.frontend PRIVATE -fno-rtti)
endif()

# LLVM/Clang headers are noisy and not warning-clean under this project's flags;
# take them as SYSTEM so -Werror stays meaningful for our own code. PUBLIC
# because the front end's own component headers name Clang types, and every
# header in the FILE_SET is compiled on its own against the target's interface
# (VERIFY_INTERFACE_HEADER_SETS, and through it the style gates). specgen is a
# tool, not a library, so only the driver and the tests see them (decision
# tool-not-library; docs/plans/frontend-split.md#clang-header-home).
target_include_directories(
    beman.specgen.frontend
    SYSTEM
    PUBLIC ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS}
)

# How Clang reaches the driver. `shared` is the distribution build's
# clang-cpp + LLVM, sufficient for both the tooling and formatting surfaces.
# `static` links the Clang component archives the front end actually uses,
# which is what a portable binary needs (docs/plans/manylinux-release-binary.md,
# static-llvm-link); `auto` takes `shared` when the LLVM it was given has a
# clang-cpp and `static` when it does not, as an LLVM built for the release
# image does not.
#
# The LLVM side follows from the Clang one rather than being chosen here: the
# exported Clang targets name whichever LLVM their own build linked. An LLVM
# built with LLVM_LINK_LLVM_DYLIB (Debian's) names the shared `LLVM`, so a
# `static` build against it still loads libLLVM; one built without it names
# the static LLVM components, and the driver loads no LLVM library at all.
# Naming the LLVM components here as well would put two copies of LLVM in one
# process, which fails at startup when both register the same options.
set(BEMAN_SPECGEN_LLVM_LINK
    "auto"
    CACHE STRING
    "How the front end links Clang: auto, shared, or static"
)
set_property(CACHE BEMAN_SPECGEN_LLVM_LINK PROPERTY STRINGS auto shared static)
set(specgen_llvm_link "${BEMAN_SPECGEN_LLVM_LINK}")
if(specgen_llvm_link STREQUAL "auto")
    if(TARGET clang-cpp)
        set(specgen_llvm_link "shared")
    else()
        set(specgen_llvm_link "static")
    endif()
endif()
if(specgen_llvm_link STREQUAL "shared")
    if(NOT TARGET clang-cpp)
        message(
            FATAL_ERROR
            "beman.specgen: BEMAN_SPECGEN_LLVM_LINK=shared, but this LLVM has no clang-cpp"
        )
    endif()
    set(specgen_clang_libraries clang-cpp LLVM)
elseif(specgen_llvm_link STREQUAL "static")
    # clangOptions holds clang::GetResourcesPath (parse_header.cpp,
    # bundled_resource_dir_args); the rest are the tooling and format
    # surfaces and what they are built on.
    set(specgen_clang_libraries
        clangTooling
        clangToolingCore
        clangToolingInclusions
        clangFormat
        clangFrontend
        clangSerialization
        clangDriver
        clangOptions
        clangParse
        clangSema
        clangAnalysis
        clangEdit
        clangRewrite
        clangLex
        clangAST
        clangBasic
        clangSupport
    )
    # Let the linker drop what the front end never reaches, which is most of
    # a static LLVM.
    target_compile_options(
        beman.specgen.frontend
        PRIVATE -ffunction-sections -fdata-sections
    )
    target_link_options(beman.specgen.frontend INTERFACE -Wl,--gc-sections)
else()
    message(
        FATAL_ERROR
        "beman.specgen: BEMAN_SPECGEN_LLVM_LINK must be auto, shared, or static, not '${BEMAN_SPECGEN_LLVM_LINK}'"
    )
endif()
message(STATUS "beman.specgen: front end links Clang ${specgen_llvm_link}")

# beman::specgen is PUBLIC because the tier's contract type, ir::Document,
# comes from the core.
target_link_libraries(
    beman.specgen.frontend
    PUBLIC beman::specgen
    PRIVATE ${specgen_clang_libraries}
)
