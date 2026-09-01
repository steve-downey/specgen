# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set(ALL_EXAMPLES
    todo
    fixpoint_tree_example
    expression_algorithms
    binary_tree_adapt
    nonce_tree_direct
    search_tree_on_fix
    sequence_algorithms
)
message("Examples to be built: ${ALL_EXAMPLES}")

foreach(example ${ALL_EXAMPLES})
    add_executable(beman.tree_algorithms.examples.${example})
    target_sources(
        beman.tree_algorithms.examples.${example}
        PRIVATE ${example}.cpp
    )
    target_link_libraries(
        beman.tree_algorithms.examples.${example}
        PRIVATE beman::tree_algorithms
    )
    if(BEMAN_TREE_ALGORITHMS_USE_MODULES)
        set_target_properties(
            beman.tree_algorithms.examples.${example}
            PROPERTIES CXX_MODULE_STD ON
        )
    endif()
endforeach()
