include_guard(GLOBAL)

set(CMAKE_CXX_STANDARD 23)

# This is a constexpr-heavy library: trees are built and folded at compile
# time (tests, examples, and the compile-time benchmarks all do it), so the
# default constexpr operation ceiling is lifted project-wide. Applied to the
# base flags so every configuration is coherent.
set(CMAKE_CXX_FLAGS
    "-Wall -Wextra -std=gnu++23 -fconstexpr-ops-limit=1000000000"
    CACHE STRING
    "CXX_FLAGS"
    FORCE
)

set(CMAKE_CXX_FLAGS_DEBUG
    "-O0 -fno-inline -g3"
    CACHE STRING
    "C++ DEBUG Flags"
    FORCE
)
set(CMAKE_CXX_FLAGS_RELEASE
    "-Ofast -g0 -DNDEBUG"
    CACHE STRING
    "C++ Release Flags"
    FORCE
)
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO
    "-O3 -g -DNDEBUG"
    CACHE STRING
    "C++ RelWithDebInfo Flags"
    FORCE
)
set(CMAKE_CXX_FLAGS_TSAN
    "-O3 -g -fsanitize=thread"
    CACHE STRING
    "C++ TSAN Flags"
    FORCE
)
set(CMAKE_CXX_FLAGS_ASAN
    "-O3 -g -fsanitize=address,undefined,leak"
    CACHE STRING
    "C++ ASAN Flags"
    FORCE
)

# Benchmark configuration: optimized, symbols kept, asserts off, frame
# pointers preserved for profilers. Deliberately unsanitized — benchmarks
# are measured here (make bench / BENCH_CONFIG=Bench), never under Asan/Tsan.
set(CMAKE_CXX_FLAGS_BENCH
    "-O3 -g -DNDEBUG -fno-omit-frame-pointer"
    CACHE STRING
    "C++ Benchmark Flags"
    FORCE
)

set(CMAKE_CXX_FLAGS_GCOV
    "-O0 -fno-default-inline -fno-inline -g --coverage -fprofile-abs-path"
    CACHE STRING
    "C++ GCOV Flags"
    FORCE
)

set(CMAKE_LINKER_FLAGS_GCOV "--coverage" CACHE STRING "Linker GCOV Flags" FORCE)
