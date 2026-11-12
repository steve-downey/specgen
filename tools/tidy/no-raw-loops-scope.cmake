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
#         [-DDB_OUT=<file>] -P tools/tidy/no-raw-loops-scope.cmake
#
# Writes one entry per line, sorted: to OUT when given (the form the pass
# driver consumes), otherwise to stdout. Exactly the text gate's exclusions
# apply — tests/ and vendor/ (repo-relative), any *.test.cpp — plus _deps/,
# since the fetched Catch2 sources appear in the database once tests are
# configured; examples/ is in scope (examples-scope decision, recorded in the
# plan). Everything else stays, the build-tree header-verification TUs
# included: their findings land in the real headers they include.
#
# DB_OUT, when given, receives the kept entries as a compile database of
# their own, one entry per file, with the flags that do not survive a clang
# re-parse scrubbed from each command:
# -fsanitize*/-fno-sanitize*, --coverage/-fprofile*, -Werror, and the GCC
# module-scanning set CMake adds under a scanning GCC (-fmodules-ts,
# -fmodule-mapper=, -fdeps-format=, -fdeps-file=, -fdeps-target=). The pass
# re-parses with clang, and a configuration's GCC-only instrumentation
# spelling is a hard error there (-fprofile-abs-path is unknown to clang)
# or an "argument unused" warning that a -Werror configuration promotes to
# one; the scanning flags are unknown arguments outright — while none of it
# changes what the check reads, which is the point: the gate gives one
# answer in every configuration. One entry per file for the same reason: a
# multi-config database (the Makefile lane) lists every file once per
# configuration, and clang-tidy would parse it once per entry, reporting
# each finding that many times over; the first entry in the database is the
# one kept. The style gates point clang-tidy's -p at DB_OUT's directory
# instead of the build's.
#
# EXPECTED, when given, names the scope the style gates' edges were written
# for at configure time (tools/tidy/style-gates.cmake), one file per line.
# The two lists must agree: a TU the database compiles but no edge checks
# would pass unchecked, and one the edges name but the database lacks has no
# command to parse with. Both outputs are rewritten only when their content
# changes, so the edges that depend on DB_OUT rerun only when a compile
# command moved.

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
set(_db_entries "")
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
    if(DEFINED DB_OUT AND NOT "${_file}" IN_LIST _files)
        # The flags live in the entry's "command" member; the tokens being
        # deleted cannot occur in its "directory"/"file"/"output" paths, so
        # scrubbing the serialized entry keeps the JSON escaping intact.
        string(JSON _entry GET "${_db}" ${_idx})
        string(REGEX REPLACE " -f(no-)?sanitize[^ \"]*" "" _entry "${_entry}")
        string(
            REGEX REPLACE " --coverage| -fprofile[^ \"]*| -Werror[^ \"]*"
            ""
            _entry
            "${_entry}"
        )
        string(
            REGEX REPLACE
                " -fmodules-ts| -fmodule-mapper=[^ \"]*| -fdeps-[a-z]*=[^ \"]*"
            ""
            _entry
            "${_entry}"
        )
        # Accumulate as text, never through a CMake list: a command string
        # is free to contain a semicolon.
        if(_db_entries)
            string(APPEND _db_entries ",\n${_entry}")
        else()
            set(_db_entries "${_entry}")
        endif()
    endif()
    list(APPEND _files "${_file}")
endforeach()

list(REMOVE_DUPLICATES _files)
list(SORT _files)

list(LENGTH _files _kept)
if(_kept EQUAL 0)
    message(FATAL_ERROR "${COMPILE_DB} yields an empty scope; nothing to gate")
endif()

# Writes content to path only when it differs from what is there, so the
# file's timestamp moves only when its content does.
function(_write_if_different path content)
    file(WRITE "${path}.tmp" "${content}")
    file(COPY_FILE "${path}.tmp" "${path}" ONLY_IF_DIFFERENT)
    file(REMOVE "${path}.tmp")
endfunction()

if(DEFINED EXPECTED)
    file(STRINGS "${EXPECTED}" _expected)
    set(_unchecked ${_files})
    list(REMOVE_ITEM _unchecked ${_expected})
    set(_uncompiled ${_expected})
    list(REMOVE_ITEM _uncompiled ${_files})
    if(_unchecked OR _uncompiled)
        list(JOIN _unchecked "\n  " _unchecked)
        list(JOIN _uncompiled "\n  " _uncompiled)
        message(
            FATAL_ERROR
            "the style gates' scope disagrees with ${COMPILE_DB}:\n"
            "in the database, no edge:\n  ${_unchecked}\n"
            "an edge, not in the database:\n  ${_uncompiled}\n"
            "(tools/tidy/style-gates.cmake lists the TUs at configure time)"
        )
    endif()
endif()

if(DEFINED OUT)
    list(JOIN _files "\n" _joined)
    _write_if_different("${OUT}" "${_joined}\n")
else()
    foreach(_file IN LISTS _files)
        message(NOTICE "${_file}")
    endforeach()
endif()

if(DEFINED DB_OUT)
    _write_if_different("${DB_OUT}" "[\n${_db_entries}\n]\n")
endif()
