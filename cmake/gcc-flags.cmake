# cmake/gcc-flags.cmake                                             -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

include_guard(GLOBAL)

set(CMAKE_CXX_STANDARD 26)

set(CMAKE_CXX_FLAGS "-Wall -Wextra -std=gnu++26" CACHE STRING "CXX_FLAGS" FORCE)

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

set(CMAKE_CXX_FLAGS_GCOV
    "-O0 -fno-default-inline -fno-inline -g --coverage -fprofile-abs-path"
    CACHE STRING
    "C++ GCOV Flags"
    FORCE
)

set(CMAKE_LINKER_FLAGS_GCOV "--coverage" CACHE STRING "Linker GCOV Flags" FORCE)

# The standard floor is C++26 (decision cxx26-baseline), which needs a recent GCC (16) whose
# libstdc++ is typically not on the system's default runtime search path. Embed
# an rpath to the *compiler's own* libstdc++ directory, queried from the compiler
# so it stays correct on any machine, so the built binaries run without a
# hand-set LD_LIBRARY_PATH. Requires the including toolchain to set
# CMAKE_CXX_COMPILER before this file; guarded so toolchains that include it
# earlier simply skip the rpath rather than erroring.
if(CMAKE_CXX_COMPILER)
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=libstdc++.so
        OUTPUT_VARIABLE _beman_libstdcxx
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    get_filename_component(_beman_libstdcxx_dir "${_beman_libstdcxx}" DIRECTORY)
    if(_beman_libstdcxx_dir AND IS_DIRECTORY "${_beman_libstdcxx_dir}")
        set(CMAKE_EXE_LINKER_FLAGS
            "-Wl,-rpath,${_beman_libstdcxx_dir}"
            CACHE STRING
            "Executable linker flags"
            FORCE
        )
        set(CMAKE_SHARED_LINKER_FLAGS
            "-Wl,-rpath,${_beman_libstdcxx_dir}"
            CACHE STRING
            "Shared linker flags"
            FORCE
        )
    endif()
endif()
