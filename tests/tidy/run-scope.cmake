# tests/tidy/run-scope.cmake                                      -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The scope script over a database shaped like the Makefile lane's: a
# multi-config generator lists every file once per configuration, and a
# scanning GCC adds the module-scanning flags clang rejects as unknown
# arguments. The pass must see one entry per file with no instrumentation
# or scanning flag left on it, and the tests/ exclusion must still apply.
#
#   cmake -DSCOPE_SCRIPT=<tools/tidy/no-raw-loops-scope.cmake>
#         -DREPO_ROOT=<repo> -DSCRATCH=<dir> -P run-scope.cmake

foreach(_required SCOPE_SCRIPT REPO_ROOT SCRATCH)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "pass -D${_required}=...")
    endif()
endforeach()

file(MAKE_DIRECTORY "${SCRATCH}")
set(_db "${SCRATCH}/compile_commands.json")
set(_kept "${REPO_ROOT}/src/beman/specgen/ir.cpp")
set(_dropped "${REPO_ROOT}/tests/beman/specgen/ir.test.cpp")
set(_scan
    "-fmodules-ts -fmodule-mapper=CMakeFiles/x.dir/Asan/ir.cpp.o.modmap -MD -fdeps-format=p1689r5 -fdeps-file=ir.ddi -fdeps-target=ir.o"
)
file(
    WRITE "${_db}"
    "[
{\"directory\": \"${SCRATCH}\", \"command\": \"g++ -O3 -g -fsanitize=address,undefined,leak -Werror ${_scan} -o Asan/ir.o -c ${_kept}\", \"file\": \"${_kept}\", \"output\": \"Asan/ir.o\"},
{\"directory\": \"${SCRATCH}\", \"command\": \"g++ -O0 -g3 ${_scan} -o Debug/ir.o -c ${_kept}\", \"file\": \"${_kept}\", \"output\": \"Debug/ir.o\"},
{\"directory\": \"${SCRATCH}\", \"command\": \"g++ -O3 -g ${_scan} -o Asan/ir.test.o -c ${_dropped}\", \"file\": \"${_dropped}\", \"output\": \"Asan/ir.test.o\"}
]
"
)

execute_process(
    COMMAND
        ${CMAKE_COMMAND} "-DCOMPILE_DB=${_db}" "-DOUT=${SCRATCH}/scope.txt"
        "-DDB_OUT=${SCRATCH}/scrubbed.json" -P "${SCOPE_SCRIPT}"
    RESULT_VARIABLE _status
    ERROR_VARIABLE _err
)
if(NOT _status EQUAL 0)
    message(FATAL_ERROR "scope script failed:\n${_err}")
endif()

file(STRINGS "${SCRATCH}/scope.txt" _files)
if(NOT _files STREQUAL "${_kept}")
    message(FATAL_ERROR "expected the one kept file, got: ${_files}")
endif()

file(READ "${SCRATCH}/scrubbed.json" _scrubbed)
string(JSON _count LENGTH "${_scrubbed}")
if(NOT _count EQUAL 1)
    message(
        FATAL_ERROR
        "expected one entry for the one kept file, got ${_count}:\n${_scrubbed}"
    )
endif()
string(JSON _command GET "${_scrubbed}" 0 "command")
foreach(
    _flag
    -fsanitize
    -Werror
    -fmodules-ts
    -fmodule-mapper
    -fdeps-format
    -fdeps-file
    -fdeps-target
)
    if(_command MATCHES "${_flag}")
        message(FATAL_ERROR "${_flag} survived the scrub: ${_command}")
    endif()
endforeach()
if(NOT _command MATCHES "^g\\+\\+ -O3 -g -MD -o Asan/ir.o -c ")
    message(FATAL_ERROR "the first entry was not the one kept: ${_command}")
endif()
