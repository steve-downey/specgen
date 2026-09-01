# include/beman/specgen/CMakeLists.txt                             -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if(BEMAN_SPECGEN_USE_MODULES)
    target_sources(
        beman.specgen
        PUBLIC
            FILE_SET CXX_MODULES FILES specgen.cppm
            FILE_SET HEADERS
                FILES
                    config.hpp
                    conjuncts.hpp
                    diagnostic.hpp
                    docblock.hpp
                    document_build.hpp
                    foundation/fold_left_short.hpp
                    foundation/json_descriptor.hpp
                    foundation/json_writer.hpp
                    foundation/monoid.hpp
                    foundation/overloaded.hpp
                    foundation/traverse.hpp
                    fragments.hpp
                    ir.hpp
                    ir_fold.hpp
                    lower.hpp
                    markers.hpp
                    specgen.hpp
                    "${PROJECT_BINARY_DIR}/include/beman/specgen/config_generated.hpp"
    )
else()
    target_sources(
        beman.specgen
        PUBLIC
            FILE_SET HEADERS
                FILES
                    config.hpp
                    conjuncts.hpp
                    diagnostic.hpp
                    docblock.hpp
                    document_build.hpp
                    foundation/fold_left_short.hpp
                    foundation/json_descriptor.hpp
                    foundation/json_writer.hpp
                    foundation/monoid.hpp
                    foundation/overloaded.hpp
                    foundation/traverse.hpp
                    fragments.hpp
                    ir.hpp
                    ir_fold.hpp
                    lower.hpp
                    markers.hpp
                    specgen.hpp
                    "${PROJECT_BINARY_DIR}/include/beman/specgen/config_generated.hpp"
    )
endif()

add_subdirectory(backend)
add_subdirectory(foundation/parse)
add_subdirectory(validate)
