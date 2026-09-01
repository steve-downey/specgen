# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if(BEMAN_SPECGEN_USE_MODULES)
    target_sources(
        beman.specgen
        PUBLIC
            FILE_SET CXX_MODULES FILES specgen.cppm
            FILE_SET HEADERS
                FILES
                    config.hpp
                    specgen.hpp
                    todo.hpp
                    "${PROJECT_BINARY_DIR}/include/beman/specgen/config_generated.hpp"
    )
else()
    target_sources(
        beman.specgen
        PUBLIC
            FILE_SET HEADERS
                FILES
                    config.hpp
                    specgen.hpp
                    todo.hpp
                    "${PROJECT_BINARY_DIR}/include/beman/specgen/config_generated.hpp"
    )
endif()
