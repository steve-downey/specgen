# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set(ALL_EXAMPLES todo)
message("Examples to be built: ${ALL_EXAMPLES}")

foreach(example ${ALL_EXAMPLES})
    add_executable(beman.specgen.examples.${example})
    target_sources(beman.specgen.examples.${example} PRIVATE ${example}.cpp)
    target_link_libraries(
        beman.specgen.examples.${example}
        PRIVATE beman::specgen
    )
    if(BEMAN_SPECGEN_USE_MODULES)
        set_target_properties(
            beman.specgen.examples.${example}
            PROPERTIES CXX_MODULE_STD ON
        )
    endif()
endforeach()
