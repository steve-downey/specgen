# include/beman/specgen/backend/CMakeLists.txt                     -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

target_sources(
    beman.specgen
    PUBLIC
        FILE_SET HEADERS
            FILES
                code_spans.hpp
                draft_spans.hpp
                element_label.hpp
                render_fold.hpp
                latex.hpp
                mpark.hpp
                org.hpp
)
