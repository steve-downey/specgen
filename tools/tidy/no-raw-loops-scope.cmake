# tools/tidy/no-raw-loops-scope.cmake                              -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Turn a compile database into the specgen-no-raw-loops pass's file list
# (docs/plans/no-raw-loops-tidy-plugin.md, "The pass"). The database is the
# source of truth for what the tree compiles: production translation units
# appear because they build, and with CMAKE_VERIFY_INTERFACE_HEADER_SETS on
# in the gcc-release preset every FILE_SET header appears as its own
# verification TU — that is the machinery for "every header as its own TU",
# and nothing bespoke.
#
#   cmake -DCOMPILE_DB=<build>/compile_commands.json [-DOUT=<file>]
#         -P tools/tidy/no-raw-loops-scope.cmake
#
# Writes one entry per line, sorted: to OUT when given (the form the pass
# driver consumes), otherwise to stdout. Exactly the text gate's exclusions
# apply — tests/ and vendor/ (repo-relative), any *.test.cpp — plus _deps/,
# since the fetched Catch2 sources appear in the database once tests are
# configured; examples/ is in scope (examples-scope decision, recorded in the
# plan). Everything else stays, the build-tree header-verification TUs
# included: their findings land in the real headers they include.

if(NOT DEFINED COMPILE_DB)
    message(FATAL_ERROR "pass -DCOMPILE_DB=<build>/compile_commands.json")
endif()
if(NOT EXISTS "${COMPILE_DB}")
    message(
        FATAL_ERROR
        "no compile database at ${COMPILE_DB}; configure the preset first"
    )
endif()

get_filename_component(_repo_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
get_filename_component(_repo_root "${_repo_root}" DIRECTORY)

file(READ "${COMPILE_DB}" _db)
string(JSON _count LENGTH "${_db}")

set(_files "")
math(EXPR _last "${_count} - 1")
foreach(_idx RANGE 0 ${_last})
    string(JSON _file GET "${_db}" ${_idx} "file")
    file(RELATIVE_PATH _rel "${_repo_root}" "${_file}")
    if(_rel MATCHES "^tests/" OR _rel MATCHES "^vendor/")
        continue()
    endif()
    if(_rel MATCHES "\\.test\\.cpp$" OR _file MATCHES "/_deps/")
        continue()
    endif()
    list(APPEND _files "${_file}")
endforeach()

list(REMOVE_DUPLICATES _files)
list(SORT _files)

list(LENGTH _files _kept)
if(_kept EQUAL 0)
    message(FATAL_ERROR "${COMPILE_DB} yields an empty scope; nothing to gate")
endif()

if(DEFINED OUT)
    list(JOIN _files "\n" _joined)
    file(WRITE "${OUT}" "${_joined}\n")
else()
    foreach(_file IN LISTS _files)
        message(NOTICE "${_file}")
    endforeach()
endif()
