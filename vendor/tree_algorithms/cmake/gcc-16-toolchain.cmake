include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/gcc-flags.cmake")

set(CMAKE_C_COMPILER gcc-16)
set(CMAKE_CXX_COMPILER g++-16)
set(GCOV_EXECUTABLE "gcov-16" CACHE STRING "GCOV executable" FORCE)

# gcc-16 is the project's C++26 vanguard toolchain: it targets gnu++26 so
# C++26-only facilities (e.g. std::indirect) are available. The other
# compiler-version toolchains stay at the project baseline (gnu++23).
set(CMAKE_CXX_STANDARD 26)
string(
    REPLACE "-std=gnu++23"
    "-std=gnu++26"
    CMAKE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS}"
)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" CACHE STRING "CXX_FLAGS" FORCE)

set(CMAKE_CXX_FLAGS_ASAN
    "${CMAKE_CXX_FLAGS_ASAN} -Wno-maybe-uninitialized"
    CACHE STRING
    "C++ ASAN Flags"
    FORCE
)
