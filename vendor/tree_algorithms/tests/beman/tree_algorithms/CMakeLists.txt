# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

find_package(Catch2 3 REQUIRED)

add_executable(beman.tree_algorithms.tests.todo)
target_sources(beman.tree_algorithms.tests.todo PRIVATE todo.test.cpp)
target_link_libraries(
    beman.tree_algorithms.tests.todo
    PRIVATE beman::tree_algorithms Catch2::Catch2WithMain
)
if(BEMAN_EXEMPLAR_USE_MODULES)
    set_target_properties(
        beman.tree_algorithms.tests.todo
        PROPERTIES CXX_MODULE_STD ON
    )
endif()

add_executable(beman.tree_algorithms.tests.fold_map)
target_sources(beman.tree_algorithms.tests.fold_map PRIVATE fold_map.test.cpp)
target_link_libraries(
    beman.tree_algorithms.tests.fold_map
    PRIVATE beman::tree_algorithms Catch2::Catch2WithMain
)
if(BEMAN_TREE_ALGORITHMS_USE_MODULES)
    set_target_properties(
        beman.tree_algorithms.tests.fold_map
        PROPERTIES CXX_MODULE_STD ON
    )
endif()

add_executable(beman.tree_algorithms.tests.fringe_tree)
target_sources(
    beman.tree_algorithms.tests.fringe_tree
    PRIVATE fringe_tree.test.cpp
)
target_link_libraries(
    beman.tree_algorithms.tests.fringe_tree
    PRIVATE beman::tree_algorithms Catch2::Catch2WithMain
)
if(BEMAN_TREE_ALGORITHMS_USE_MODULES)
    set_target_properties(
        beman.tree_algorithms.tests.fringe_tree
        PROPERTIES CXX_MODULE_STD ON
    )
endif()

add_executable(beman.tree_algorithms.tests.box)
target_sources(beman.tree_algorithms.tests.box PRIVATE box.test.cpp)
target_link_libraries(
    beman.tree_algorithms.tests.box
    PRIVATE beman::tree_algorithms Catch2::Catch2WithMain
)
if(BEMAN_TREE_ALGORITHMS_USE_MODULES)
    set_target_properties(
        beman.tree_algorithms.tests.box
        PROPERTIES CXX_MODULE_STD ON
    )
endif()

add_executable(beman.tree_algorithms.tests.child_slot)
target_sources(
    beman.tree_algorithms.tests.child_slot
    PRIVATE child_slot.test.cpp
)
target_link_libraries(
    beman.tree_algorithms.tests.child_slot
    PRIVATE beman::tree_algorithms Catch2::Catch2WithMain
)
if(BEMAN_TREE_ALGORITHMS_USE_MODULES)
    set_target_properties(
        beman.tree_algorithms.tests.child_slot
        PROPERTIES CXX_MODULE_STD ON
    )
endif()

add_executable(beman.tree_algorithms.tests.fix)
target_sources(beman.tree_algorithms.tests.fix PRIVATE fix.test.cpp)
target_link_libraries(
    beman.tree_algorithms.tests.fix
    PRIVATE beman::tree_algorithms Catch2::Catch2WithMain
)
if(BEMAN_TREE_ALGORITHMS_USE_MODULES)
    set_target_properties(
        beman.tree_algorithms.tests.fix
        PROPERTIES CXX_MODULE_STD ON
    )
endif()

add_executable(beman.tree_algorithms.tests.overloaded)
target_sources(
    beman.tree_algorithms.tests.overloaded
    PRIVATE overloaded.test.cpp
)
target_link_libraries(
    beman.tree_algorithms.tests.overloaded
    PRIVATE beman::tree_algorithms Catch2::Catch2WithMain
)
if(BEMAN_TREE_ALGORITHMS_USE_MODULES)
    set_target_properties(
        beman.tree_algorithms.tests.overloaded
        PROPERTIES CXX_MODULE_STD ON
    )
endif()

add_executable(beman.tree_algorithms.tests.recursion_schemes)
target_sources(
    beman.tree_algorithms.tests.recursion_schemes
    PRIVATE recursion_schemes.test.cpp
)
target_link_libraries(
    beman.tree_algorithms.tests.recursion_schemes
    PRIVATE beman::tree_algorithms Catch2::Catch2WithMain
)
if(BEMAN_TREE_ALGORITHMS_USE_MODULES)
    set_target_properties(
        beman.tree_algorithms.tests.recursion_schemes
        PROPERTIES CXX_MODULE_STD ON
    )
endif()

add_executable(beman.tree_algorithms.tests.functor)
target_sources(beman.tree_algorithms.tests.functor PRIVATE functor.test.cpp)
target_link_libraries(
    beman.tree_algorithms.tests.functor
    PRIVATE beman::tree_algorithms Catch2::Catch2WithMain
)
if(BEMAN_TREE_ALGORITHMS_USE_MODULES)
    set_target_properties(
        beman.tree_algorithms.tests.functor
        PROPERTIES CXX_MODULE_STD ON
    )
endif()

add_executable(beman.tree_algorithms.tests.recursion_schemes_lookup)
target_sources(
    beman.tree_algorithms.tests.recursion_schemes_lookup
    PRIVATE recursion_schemes_lookup.test.cpp
)
target_link_libraries(
    beman.tree_algorithms.tests.recursion_schemes_lookup
    PRIVATE beman::tree_algorithms Catch2::Catch2WithMain
)
if(BEMAN_TREE_ALGORITHMS_USE_MODULES)
    set_target_properties(
        beman.tree_algorithms.tests.recursion_schemes_lookup
        PROPERTIES CXX_MODULE_STD ON
    )
endif()

add_executable(beman.tree_algorithms.tests.expression)
target_sources(
    beman.tree_algorithms.tests.expression
    PRIVATE expression.test.cpp
)
target_link_libraries(
    beman.tree_algorithms.tests.expression
    PRIVATE beman::tree_algorithms Catch2::Catch2WithMain
)
if(BEMAN_TREE_ALGORITHMS_USE_MODULES)
    set_target_properties(
        beman.tree_algorithms.tests.expression
        PROPERTIES CXX_MODULE_STD ON
    )
endif()

add_executable(beman.tree_algorithms.tests.functors)
target_sources(beman.tree_algorithms.tests.functors PRIVATE functors.test.cpp)
target_link_libraries(
    beman.tree_algorithms.tests.functors
    PRIVATE beman::tree_algorithms Catch2::Catch2WithMain
)
if(BEMAN_TREE_ALGORITHMS_USE_MODULES)
    set_target_properties(
        beman.tree_algorithms.tests.functors
        PROPERTIES CXX_MODULE_STD ON
    )
endif()

add_executable(beman.tree_algorithms.tests.binary_tree)
target_sources(
    beman.tree_algorithms.tests.binary_tree
    PRIVATE binary_tree.test.cpp
)
target_link_libraries(
    beman.tree_algorithms.tests.binary_tree
    PRIVATE beman::tree_algorithms Catch2::Catch2WithMain
)
if(BEMAN_TREE_ALGORITHMS_USE_MODULES)
    set_target_properties(
        beman.tree_algorithms.tests.binary_tree
        PROPERTIES CXX_MODULE_STD ON
    )
endif()

add_executable(beman.tree_algorithms.tests.rose_tree)
target_sources(beman.tree_algorithms.tests.rose_tree PRIVATE rose_tree.test.cpp)
target_link_libraries(
    beman.tree_algorithms.tests.rose_tree
    PRIVATE beman::tree_algorithms Catch2::Catch2WithMain
)
if(BEMAN_TREE_ALGORITHMS_USE_MODULES)
    set_target_properties(
        beman.tree_algorithms.tests.rose_tree
        PROPERTIES CXX_MODULE_STD ON
    )
endif()

# The fetch-content dependency provider makes Catch2 available inside a
# function scope, so its extras/ directory never reaches CMAKE_MODULE_PATH
# and catch2_SOURCE_DIR is not visible here; recover it from FetchContent's
# global properties. (The vcpkg path needs neither: Catch2's installed
# config puts Catch.cmake on the module path itself.)
if(NOT COMMAND catch_discover_tests)
    include(FetchContent)
    FetchContent_GetProperties(catch2)
    if(catch2_SOURCE_DIR)
        list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
    endif()
endif()
include(Catch)
catch_discover_tests(beman.tree_algorithms.tests.todo)
catch_discover_tests(beman.tree_algorithms.tests.box)
catch_discover_tests(beman.tree_algorithms.tests.child_slot)
catch_discover_tests(beman.tree_algorithms.tests.fix)
catch_discover_tests(beman.tree_algorithms.tests.overloaded)
catch_discover_tests(beman.tree_algorithms.tests.recursion_schemes)
catch_discover_tests(beman.tree_algorithms.tests.functor)
catch_discover_tests(beman.tree_algorithms.tests.recursion_schemes_lookup)
catch_discover_tests(beman.tree_algorithms.tests.expression)
catch_discover_tests(beman.tree_algorithms.tests.functors)
catch_discover_tests(beman.tree_algorithms.tests.binary_tree)
catch_discover_tests(beman.tree_algorithms.tests.fold_map)
catch_discover_tests(beman.tree_algorithms.tests.fringe_tree)
catch_discover_tests(beman.tree_algorithms.tests.rose_tree)
