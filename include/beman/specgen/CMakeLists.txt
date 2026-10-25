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
                    depfile.hpp
                    diagnostic.hpp
                    docblock.hpp
                    document_build.hpp
                    foundation/enumeration.hpp
                    foundation/fold_left_short.hpp
                    foundation/monoid.hpp
                    foundation/overloaded.hpp
                    fragments.hpp
                    ir.hpp
                    ir_fold.hpp
                    ir_json.hpp
                    lower.hpp
                    markers.hpp
                    rsec.hpp
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
                    depfile.hpp
                    diagnostic.hpp
                    docblock.hpp
                    document_build.hpp
                    foundation/enumeration.hpp
                    foundation/fold_left_short.hpp
                    foundation/monoid.hpp
                    foundation/overloaded.hpp
                    fragments.hpp
                    ir.hpp
                    ir_fold.hpp
                    ir_json.hpp
                    lower.hpp
                    markers.hpp
                    rsec.hpp
                    "${PROJECT_BINARY_DIR}/include/beman/specgen/config_generated.hpp"
    )
endif()

add_subdirectory(backend)
add_subdirectory(json)
add_subdirectory(parse)
add_subdirectory(validate)
