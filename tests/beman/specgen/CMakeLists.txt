# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

find_package(Catch2 3 REQUIRED)

add_executable(beman.specgen.tests.todo)
target_sources(beman.specgen.tests.todo PRIVATE todo.test.cpp)
target_link_libraries(
    beman.specgen.tests.todo
    PRIVATE beman::specgen Catch2::Catch2WithMain
)
if(BEMAN_SPECGEN_USE_MODULES)
    set_target_properties(beman.specgen.tests.todo PROPERTIES CXX_MODULE_STD ON)
endif()

include(Catch)
catch_discover_tests(beman.specgen.tests.todo)
