# tools/tidy/style-gates.cmake                                      -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The clang-tidy style gates as build edges (docs/plans/no-raw-loops-tidy-plugin.md,
# "The pass"). One custom command per check per translation unit, each
# running a single clang-tidy process and touching a stamp. The build tool
# schedules them exactly as it schedules the compiler: under its own -j, never
# more than one process per job slot. An earlier version ran run-clang-tidy
# from a ctest case; run-clang-tidy defaults to one process per CPU, so every
# gate hid a whole machine's worth of clang-tidy inside one ctest slot, and
# three gates under a parallel ctest took a 20-core box down. Nothing here
# fans out on its own.
#
# Included from the top-level CMakeLists.txt after every target is defined.
# Defines:
#
#   style.<check>   one target per check: every stamp for that check
#   style           all of them
#
# The ctest cases style.<check> build their target with -j 1 (see
# CMakeLists.txt), so they are a no-op once the build has run the edges and a
# serial rerun of just the failing ones otherwise.
#
# Each edge's dependencies are its TU, the scrubbed compile database, and the
# plugin; clang-tidy writes a depfile for the headers the TU includes (the
# `-Wp,` spelling: clang tooling strips every `-M` flag it is handed, but not
# one the driver unpacks from -Wp), so editing a header reruns the checks of
# the TUs that include it and nothing else.

# The TUs in scope, from the targets themselves, so the edges can be written
# at configure time: every compiled source of a target defined under src/,
# tools/ or examples/, and for a target that verifies its interface header
# sets, the verification TU CMake generates for each header. The scope
# script checks this list against the compile database at build time, so a
# TU the two disagree on fails the build instead of going unchecked.
function(_specgen_style_targets dir out)
    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_sub IN LISTS _subdirs)
        _specgen_style_targets("${_sub}" _sub_targets)
        list(APPEND _targets ${_sub_targets})
    endforeach()
    set(${out} ${_targets} PARENT_SCOPE)
endfunction()

function(_specgen_style_in_scope path out)
    cmake_path(
        RELATIVE_PATH path
        BASE_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE _rel
    )
    if(
        _rel MATCHES "^(tests|vendor)/"
        OR _rel MATCHES "\\.test\\.cpp$"
        OR path MATCHES "/_deps/"
        OR _rel MATCHES "^\\.\\./"
    )
        set(${out} FALSE PARENT_SCOPE)
    else()
        set(${out} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(_specgen_style_scope out)
    _specgen_style_targets("${PROJECT_SOURCE_DIR}" _targets)
    set(_tus "")
    foreach(_target IN LISTS _targets)
        get_target_property(_type ${_target} TYPE)
        if(_type STREQUAL "UTILITY" OR _type STREQUAL "GLOBAL_TARGET")
            continue()
        endif()
        get_target_property(_source_dir ${_target} SOURCE_DIR)
        get_target_property(_binary_dir ${_target} BINARY_DIR)
        if(NOT _type STREQUAL "INTERFACE_LIBRARY")
            get_target_property(_sources ${_target} SOURCES)
            foreach(_source IN LISTS _sources)
                if(
                    _source MATCHES "^\\$<"
                    OR NOT _source MATCHES "\\.(cpp|cxx|cc)$"
                )
                    continue()
                endif()
                cmake_path(
                    ABSOLUTE_PATH _source
                    BASE_DIRECTORY "${_source_dir}"
                    NORMALIZE
                )
                _specgen_style_in_scope("${_source}" _keep)
                if(_keep)
                    list(APPEND _tus "${_source}")
                endif()
            endforeach()
        endif()
        get_target_property(_verify ${_target} VERIFY_INTERFACE_HEADER_SETS)
        if(NOT _verify)
            continue()
        endif()
        _specgen_style_in_scope("${_source_dir}/x" _keep)
        if(NOT _keep)
            continue()
        endif()
        get_target_property(_sets ${_target} INTERFACE_HEADER_SETS)
        foreach(_set IN LISTS _sets)
            get_target_property(_headers ${_target} HEADER_SET_${_set})
            get_target_property(_base_dirs ${_target} HEADER_DIRS_${_set})
            foreach(_header IN LISTS _headers)
                cmake_path(
                    ABSOLUTE_PATH _header
                    BASE_DIRECTORY "${_source_dir}"
                    NORMALIZE
                )
                foreach(_base IN LISTS _base_dirs)
                    cmake_path(IS_PREFIX _base "${_header}" NORMALIZE _under)
                    if(_under)
                        cmake_path(
                            RELATIVE_PATH _header
                            BASE_DIRECTORY "${_base}"
                            OUTPUT_VARIABLE _rel
                        )
                        list(
                            APPEND _tus
                            "${_binary_dir}/${_target}_verify_interface_header_sets/${_rel}.cxx"
                        )
                        break()
                    endif()
                endforeach()
            endforeach()
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES _tus)
    list(SORT _tus)
    set(${out} ${_tus} PARENT_SCOPE)
endfunction()

function(specgen_style_gates)
    set(_checks specgen-no-raw-loops specgen-no-raw-switch misc-include-cleaner)
    add_custom_target(style)
    if(NOT BEMAN_SPECGEN_TIDY_PLUGIN_TOOLCHAIN)
        # The ctest cases fail loudly naming what is missing; the targets
        # exist so a build naming them still configures.
        foreach(_check IN LISTS _checks)
            add_custom_target(style.${_check})
            add_dependencies(style style.${_check})
        endforeach()
        return()
    endif()

    _specgen_style_scope(_tus)
    list(LENGTH _tus _count)
    if(_count EQUAL 0)
        message(
            FATAL_ERROR
            "beman.specgen: the style gates found no TUs in scope"
        )
    endif()
    message(STATUS "beman.specgen: style gates over ${_count} TUs")

    set(_dir "${CMAKE_BINARY_DIR}/style")
    list(JOIN _tus "\n" _joined)
    file(WRITE "${_dir}/expected-scope.txt.tmp" "${_joined}\n")
    file(
        COPY_FILE "${_dir}/expected-scope.txt.tmp"
        "${_dir}/expected-scope.txt"
        ONLY_IF_DIFFERENT
    )
    file(REMOVE "${_dir}/expected-scope.txt.tmp")

    # The scrubbed database the edges parse with (see no-raw-loops-scope.cmake
    # for what is scrubbed and why). Rewritten only when its content moves,
    # so a reconfigure that changes no compile command reruns nothing.
    set(_db "${_dir}/compile_commands.json")
    add_custom_command(
        OUTPUT "${_db}"
        COMMAND
            ${CMAKE_COMMAND}
            "-DCOMPILE_DB=${CMAKE_BINARY_DIR}/compile_commands.json"
            "-DOUT=${_dir}/scope.txt" "-DDB_OUT=${_db}"
            "-DEXPECTED=${_dir}/expected-scope.txt" -P
            "${PROJECT_SOURCE_DIR}/tools/tidy/no-raw-loops-scope.cmake"
        DEPENDS
            "${CMAKE_BINARY_DIR}/compile_commands.json"
            "${_dir}/expected-scope.txt"
            "${PROJECT_SOURCE_DIR}/tools/tidy/no-raw-loops-scope.cmake"
        COMMENT "Scoping the clang-tidy style gates"
        VERBATIM
    )

    # The parse uses exactly the standard library the build uses, independent
    # of clang's own GCC-installation detection (the same treatment
    # tests/tidy/run-strip.cmake pins for the checks' own tests).
    set(_extra --extra-arg=-nostdinc --extra-arg=-nostdinc++)
    foreach(_implicit IN LISTS CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
        list(APPEND _extra "--extra-arg=-isystem${_implicit}")
    endforeach()

    foreach(_check IN LISTS _checks)
        set(_stamps "")
        foreach(_tu IN LISTS _tus)
            string(MAKE_C_IDENTIFIER "${_tu}" _id)
            set(_stamp "${_dir}/$<CONFIG>/${_check}/${_id}.stamp")
            set(_depfile "${_dir}/$<CONFIG>/${_check}/${_id}.d")
            cmake_path(
                RELATIVE_PATH _tu
                BASE_DIRECTORY "${PROJECT_SOURCE_DIR}"
                OUTPUT_VARIABLE _shown
            )
            add_custom_command(
                OUTPUT "${_stamp}"
                COMMAND
                    "${BEMAN_SPECGEN_CLANG_TIDY}" -p "${_dir}" --quiet
                    "--load=$<TARGET_FILE:beman.specgen.tidy>"
                    "--checks=-*,${_check}" "--warnings-as-errors=${_check}"
                    "--header-filter=^${PROJECT_SOURCE_DIR}/(include|src|tools|examples)/"
                    ${_extra} "--extra-arg=-Wp,-MMD,${_depfile}"
                    "--extra-arg=-Wp,-MT,${_stamp}" "${_tu}"
                COMMAND ${CMAKE_COMMAND} -E touch "${_stamp}"
                DEPENDS "${_tu}" "${_db}" beman.specgen.tidy
                DEPFILE "${_depfile}"
                COMMENT "clang-tidy ${_check} ${_shown}"
                VERBATIM
            )
            list(APPEND _stamps "${_stamp}")
        endforeach()
        add_custom_target(style.${_check} DEPENDS ${_stamps})
        # Order only: whatever the targets generate (headers under the
        # binary include directory) exists before a TU naming it is parsed.
        add_dependencies(style.${_check} beman.specgen)
        add_dependencies(style style.${_check})
    endforeach()
endfunction()

specgen_style_gates()
