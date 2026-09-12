# tools/tidy/run-no-raw-loops.cmake                                -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The no-raw-loops gate's driver (docs/plans/no-raw-loops-tidy-plugin.md,
# "The pass"): filter the compile database through no-raw-loops-scope.cmake,
# then run the pinned run-clang-tidy over the resulting list with the
# specgen plugin loaded and specgen findings promoted to errors. The exit
# status is the gate. The ctest case style.no-raw-loops runs exactly this
# script, so a developer at the prompt runs the same command the gate runs:
#
#   cmake -DRUN_CLANG_TIDY=... -DCLANG_TIDY=... -DPLUGIN=<plugin.so>
#         -DBUILD_DIR=<build> -DIMPLICIT_DIRS=<dir|dir|...>
#         -P tools/tidy/run-no-raw-loops.cmake
#
# CLANG_TIDY is passed as -clang-tidy-binary every time: without it,
# run-clang-tidy silently falls back to whatever clang-tidy is on PATH, and
# a plugin built against one LLVM's headers loaded into another's binary is
# the exact mismatch decision llvm-toolchain-pin exists to prevent (it fails
# with an undefined registry symbol at --load, at best).
#
# IMPLICIT_DIRS is the configured C++ compiler's implicit include search
# path, |-separated. It is handed to every TU as explicit -isystem
# directories with -nostdinc/-nostdinc++, so the parse uses exactly the
# standard library the build uses, independent of clang's own GCC-installation
# detection — one answer on every box the tree builds on (the same treatment
# tests/tidy/run-strip.cmake pins for the check's own tests).

foreach(_required RUN_CLANG_TIDY CLANG_TIDY PLUGIN BUILD_DIR)
    if(NOT DEFINED ${_required})
        message(
            FATAL_ERROR
            "pass -D${_required}=... (see this script's header)"
        )
    endif()
endforeach()

set(_compile_db "${BUILD_DIR}/compile_commands.json")
if(NOT EXISTS "${_compile_db}")
    message(
        FATAL_ERROR
        "no compile database at ${_compile_db}; configure with "
        "CMAKE_EXPORT_COMPILE_COMMANDS on (every preset and the Makefile "
        "lane already do)"
    )
endif()

get_filename_component(_repo_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
get_filename_component(_repo_root "${_repo_root}" DIRECTORY)

# The pass re-parses with clang against its own scrubbed copy of the
# database (see DB_OUT in no-raw-loops-scope.cmake): a configuration's
# GCC-only instrumentation flags are clang parse errors or, under -Werror,
# promoted "argument unused" warnings, and none of them change what the
# check reads.
set(_scope_dir "${BUILD_DIR}/no-raw-loops")
file(MAKE_DIRECTORY "${_scope_dir}")
set(_scope_file "${_scope_dir}/scope.txt")
execute_process(
    COMMAND
        ${CMAKE_COMMAND} "-DCOMPILE_DB=${_compile_db}" "-DOUT=${_scope_file}"
        "-DDB_OUT=${_scope_dir}/compile_commands.json" -P
        "${CMAKE_CURRENT_LIST_DIR}/no-raw-loops-scope.cmake"
    RESULT_VARIABLE _scope_status
    ERROR_VARIABLE _scope_err
)
if(NOT _scope_status EQUAL 0)
    message(FATAL_ERROR "scope filtering failed:\n${_scope_err}")
endif()

file(STRINGS "${_scope_file}" _files)
list(LENGTH _files _file_count)

set(_args
    -clang-tidy-binary
    "${CLANG_TIDY}"
    -p
    "${_scope_dir}"
    -load
    "${PLUGIN}"
    -checks=-*,specgen-*
    -warnings-as-errors=specgen-*
    "-header-filter=^${_repo_root}/(include|src|tools|examples)/"
    -quiet
)
if(DEFINED IMPLICIT_DIRS)
    list(APPEND _args -extra-arg=-nostdinc -extra-arg=-nostdinc++)
    string(REPLACE "|" ";" _implicit_dirs "${IMPLICIT_DIRS}")
    foreach(_dir IN LISTS _implicit_dirs)
        list(APPEND _args "-extra-arg=-isystem${_dir}")
    endforeach()
endif()

execute_process(
    COMMAND ${RUN_CLANG_TIDY} ${_args} ${_files}
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    RESULT_VARIABLE _status
)
if(NOT _status EQUAL 0)
    message(
        FATAL_ERROR
        "specgen-no-raw-loops found raw loops outside substrate generic "
        "algorithms (or a TU failed to parse) over ${_file_count} TUs:"
        "\n${_out}\n${_err}"
    )
endif()
message(
    STATUS
    "no-raw-loops: ${_file_count} TUs clean under specgen-no-raw-loops"
)
